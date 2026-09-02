#pragma once
//
// rolltui/Style.hpp — the styling vocabulary shared by everything that draws: the
// enumerated semantic roles a renderer tags its output with, and the Style/Color a
// theme resolves each role to. Pure data. The Theme (Theme.hpp) maps Role → Style;
// renderers (Markdown.hpp, the widgets) emit Roles and never a colour; the Screen
// applies the theme when it draws. That split is what makes "no colour literal
// outside the built-in theme definitions" a grep-checkable property.
//
// The enum is THE list: a theme file missing a role inherits `text` and is reported
// once at load; a role that exists here but nowhere else is a bug the compiler can
// name (kRoleNames is sized by count).
//
#include <array>
#include <cstdint>
#include <string_view>

namespace rolltui {

enum class Role : std::uint8_t {
  // base
  text, text_muted, background, panel_background, border, border_active, title,
  label, value, accent_1, accent_2, accent_3, accent_4, prompt, note, warning, error,
  // markdown
  md_heading, md_emphasis, md_strong, md_code_inline, md_code_block, md_code_label,
  md_link, md_link_url, md_quote, md_list_marker, md_table_border, md_table_header,
  md_rule, md_strikethrough,
  // diffs
  diff_added, diff_removed, diff_context,
  // chrome
  input_text, input_cursor, input_placeholder, scroll_marker, selection, overlay,
  menu_item, menu_selected, menu_breadcrumb, menu_shortcut,
  count_
};

inline constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::count_);

inline constexpr std::array<std::string_view, kRoleCount> kRoleNames = {
    "text", "text_muted", "background", "panel_background", "border", "border_active",
    "title", "label", "value", "accent_1", "accent_2", "accent_3", "accent_4", "prompt",
    "note", "warning", "error",
    "md_heading", "md_emphasis", "md_strong", "md_code_inline", "md_code_block",
    "md_code_label", "md_link", "md_link_url", "md_quote", "md_list_marker",
    "md_table_border", "md_table_header", "md_rule", "md_strikethrough",
    "diff_added", "diff_removed", "diff_context",
    "input_text", "input_cursor", "input_placeholder", "scroll_marker", "selection",
    "overlay", "menu_item", "menu_selected", "menu_breadcrumb", "menu_shortcut",
};

inline constexpr std::string_view role_name(Role r) {
  return kRoleNames[static_cast<std::size_t>(r)];
}

// Returns Role::count_ when the name is unknown.
inline constexpr Role role_from_name(std::string_view name) {
  for (std::size_t i = 0; i < kRoleCount; ++i)
    if (kRoleNames[i] == name) return static_cast<Role>(i);
  return Role::count_;
}

struct Color {
  enum class Kind : std::uint8_t { None, Indexed, Rgb };
  Kind kind = Kind::None;
  std::uint8_t index = 0;        // Indexed: 0-255
  std::uint8_t r = 0, g = 0, b = 0;  // Rgb

  static constexpr Color none() { return {}; }
  static constexpr Color indexed(std::uint8_t i) {
    Color c;
    c.kind = Kind::Indexed;
    c.index = i;
    return c;
  }
  static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    Color c;
    c.kind = Kind::Rgb;
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
  }
  constexpr bool operator==(const Color&) const = default;
};

struct Style {
  Color fg;
  Color bg;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool dim = false;
  bool reverse = false;
  constexpr bool operator==(const Style&) const = default;
};

}  // namespace rolltui
