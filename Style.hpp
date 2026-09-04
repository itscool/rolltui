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
// PHASE 14 m2: `Color` and `Style` ARE the C structs. They are defined once, in
// `rolltui/c/rolltui_style.h`, and compiled by both languages — see that file for why one
// definition beats two plus a conversion function. Nothing about how they are USED changed:
// `Color::Kind::Rgb`, `Color::rgb(r, g, b)`, `Style{}` and `s.bold` all read as before. The
// one difference a caller can see is that the attribute bits are `unsigned char` rather
// than `bool`, so a braced init that wants a `bool` needs `!= 0`.
//
#include <array>
#include <cstdint>
#include <string_view>

#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_style.h"

namespace rolltui {

enum class Role : std::uint8_t {
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

inline constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::count_);

// THE ONE ROLE ORDINAL THAT CROSSES A C BOUNDARY, checked here rather than trusted (Phase 15
// m5). A layout node's `background` defaults to `Role::background`, and the C has to be able
// to say that in a language with no `Role` — so `rolltui/c/rolltui_layout_tree.h` carries the
// ORDINAL and this assertion ties it to the name. Naming the ordinal in one file and the role
// in another is what keeps the styling vocabulary in exactly one place (the m2 rule at
// `rolltui_diff.h`: a renderer is handed the byte it should tag with and never names a role);
// asserting it here is what stops the two drifting.
static_assert(static_cast<unsigned char>(Role::background) == ROLLTUI_ROLE_DEFAULT_BACKGROUND,
              "the C side's default node background must be Role::background");

// MUST-DIFFER pairs (milestone 15): roles a reader must be able to tell apart at a
// glance, checked by ThemeAnalysis under normal vision and under the three
// colour-vision-deficiency simulations. The list lives here, next to the enum, so a
// new role has to say what it may not be confused with (or say nothing, explicitly).
struct RolePair {
  Role a, b;
};
inline constexpr RolePair kMustDiffer[] = {
    {Role::diff_added, Role::diff_removed}, {Role::warning, Role::error},
    {Role::accent_1, Role::accent_2},       {Role::accent_1, Role::accent_3}, {Role::accent_1, Role::accent_4},
    {Role::accent_2, Role::accent_3},       {Role::accent_2, Role::accent_4}, {Role::accent_3, Role::accent_4},
    {Role::menu_item, Role::menu_selected}, {Role::input_text, Role::input_placeholder},
    // Phase 12 m4. "Which of the 17 matches am I on" is unanswerable if these two look
    // alike, so it is a must-differ pair like any other. Deliberately NOT paired with
    // `selection`: a match and a selection legitimately overlap, and the reader's
    // question there is "what did I select", which the selection winning answers.
    {Role::find_match, Role::find_current},
    // Phase 12 m5b's `diff_added_word`/`diff_removed_word` are deliberately NOT here,
    // and saying so is the point (a new role must state what it may not be confused
    // with, or say nothing EXPLICITLY). They are an EMPHASIS on a line that already
    // carries its own must-differ role and its own `+`/`-` marker: a reader who cannot
    // tell the word run from the rest of its line still knows the line changed and which
    // way. Pairing them here would demand four mutually-distinguishable fg colours under
    // three CVD simulations to buy a distinction nothing depends on.
};
// A LIMIT OF THIS CHECK, found while adding the pair above and stated here because the
// next background-carried distinction will meet it too (m5's scrollbar thumb, m6's
// effect states): the comparison below is on `fg` ONLY. Two roles that differ solely in
// `bg` — which is the natural way to draw a highlight — measure as identical and would
// fail `distinct` while looking perfectly clear on screen. That is why `find_current` is
// specified as INVERTED (dark text on the accent) rather than as the same text on a
// stronger tint: the distinction is carried where the check can see it. A theme author
// who ignores that gets told, which is the point.
inline constexpr std::size_t kMustDifferCount = sizeof(kMustDiffer) / sizeof(kMustDiffer[0]);

inline constexpr std::array<std::string_view, kRoleCount> kRoleNames = {
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

inline constexpr std::string_view role_name(Role r) {
  return kRoleNames[static_cast<std::size_t>(r)];
}

// Returns Role::count_ when the name is unknown.
inline constexpr Role role_from_name(std::string_view name) {
  for (std::size_t i = 0; i < kRoleCount; ++i)
    if (kRoleNames[i] == name) return static_cast<Role>(i);
  return Role::count_;
}

// ONE DEFINITION, in rolltui/c/rolltui_style.h, compiled by both languages. `Color::Kind`,
// `Color::none/indexed/rgb` and both `operator==`s live inside those structs under
// `#ifdef __cplusplus`, so everything a caller writes is unchanged.
using Color = RolltuiStyleColor;
using Style = RolltuiStyle;

}  // namespace rolltui
