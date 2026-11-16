/* rolltui/c/rolltui_style.c — the role name table, generated from the ONE list.
 *
 * There is deliberately no second listing here: the names come from `ROLLTUI_ROLE_LIST` in
 * the header, so a role added there gains its name in the same edit. That is the whole reason
 * the list is an X-macro (see the header's own note on why a second spelling drifts silently
 * rather than loudly). */
#include "rolltui/rolltui.h"

#include "rolltui/c/rolltui_style.h"

#include <string.h>

static const char* const kRoleNames[] = {
#define ROLLTUI_ROLE_NAME_(lower, UPPER) #lower,
    ROLLTUI_ROLE_LIST(ROLLTUI_ROLE_NAME_)
#undef ROLLTUI_ROLE_NAME_
};

const char* rolltui_role_name(unsigned char role, size_t* len) {
  const char* s = role < ROLLTUI_ROLE_COUNT ? kRoleNames[role] : "";
  if (len) *len = strlen(s);
  return s;
}

/* WHAT EACH ROLE IS FOR, in words: what the theme sheet says on a role's row, drawn in the role itself. By name, so the list's
 * order is not repeated here; a role with no words is the census's to catch. */
static const struct {
  const char* role;
  const char* words;
} kRoleWords[] = {
    {"text", "text"},
    {"text_muted", "quieter text"},
    {"background", "the screen's ground"},
    {"panel_background", "a panel's ground"},
    {"border", "a window's border"},
    {"border_active", "the focused window's border"},
    {"title", "a window's title"},
    {"label", "a label"},
    {"value", "a value beside its label"},
    {"accent_1", "the first accent"},
    {"accent_2", "the second accent"},
    {"accent_3", "the third accent"},
    {"accent_4", "the fourth accent"},
    {"prompt", "the prompt"},
    {"note", "a note"},
    {"warning", "a warning"},
    {"error", "an error"},
    {"md_heading", "a heading"},
    {"md_emphasis", "emphasis"},
    {"md_strong", "strong text"},
    {"md_code_inline", "code in a line of text"},
    {"md_code_block", "a block of code"},
    {"md_code_label", "a code block's language"},
    {"md_link", "a link"},
    {"md_link_url", "a link's address"},
    {"md_quote", "a quotation"},
    {"md_list_marker", "a list's bullet"},
    {"md_table_border", "a table's lines"},
    {"md_table_header", "a table's header"},
    {"md_rule", "a rule across the page"},
    {"md_strikethrough", "struck-out text"},
    {"diff_added", "an added line"},
    {"diff_removed", "a removed line"},
    {"diff_context", "an unchanged line"},
    {"diff_added_word", "an added word"},
    {"diff_removed_word", "a removed word"},
    {"input_text", "typed text"},
    {"input_cursor", "the caret"},
    {"input_placeholder", "a hint in an empty field"},
    {"scroll_marker", "a \"more\" marker"},
    {"selection", "selected text"},
    {"overlay", "the shade behind a popup"},
    {"menu_item", "a menu item"},
    {"menu_selected", "the menu's cursor"},
    {"menu_breadcrumb", "a menu's path and headings"},
    {"menu_shortcut", "a key beside what it does"},
    {"find_match", "a match"},
    {"find_current", "the current match"},
    {"scrollbar", "a scrollbar"},
    {"hover", "what the pointer is over"},
};

const char* rolltui_role_words(unsigned char role, size_t* len) {
  size_t i, n = 0;
  const char* name = rolltui_role_name(role, &n);
  for (i = 0; i < sizeof kRoleWords / sizeof kRoleWords[0]; ++i)
    if (strlen(kRoleWords[i].role) == n && memcmp(kRoleWords[i].role, name, n) == 0) {
      if (len) *len = strlen(kRoleWords[i].words);
      return kRoleWords[i].words;
    }
  if (len) *len = n;
  return name;
}

int rolltui_role_from_name(const char* name, size_t len) {
  size_t i;
  for (i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
    const size_t n = strlen(kRoleNames[i]);
    if (n == len && memcmp(kRoleNames[i], name, len) == 0) return (int)i;
  }
  return -1;
}

/* ---- the effect-state names, from `ROLLTUI_EFFECT_STATE_LIST` -------------------------------
 * Here rather than in `rolltui_effects.c` for one reason: this file is the library's
 * VOCABULARY table, and both lists are the same kind of thing — a closed set of names a theme
 * file is written in. Keeping them together is what stops the next one being invented
 * somewhere else. */
#include "rolltui/c/rolltui_effects.h"

static const char* const kEffectStateNames[] = {
#define ROLLTUI_EFFECT_STATE_NAME_(lower, UPPER, Camel) #lower,
    ROLLTUI_EFFECT_STATE_LIST(ROLLTUI_EFFECT_STATE_NAME_)
#undef ROLLTUI_EFFECT_STATE_NAME_
};

const char* rolltui_effect_state_name(unsigned char state, size_t* len) {
  /* "none" for anything out of range, matching the C++ this replaces: a mark whose state does
   * not exist is not marked. */
  const char* s = state < ROLLTUI_EFFECT_STATE_COUNT ? kEffectStateNames[state] : kEffectStateNames[0];
  if (len) *len = strlen(s);
  return s;
}

int rolltui_effect_state_from_name(const char* name, size_t len) {
  size_t i;
  for (i = 0; i < ROLLTUI_EFFECT_STATE_COUNT; ++i) {
    const size_t n = strlen(kEffectStateNames[i]);
    if (n == len && memcmp(kEffectStateNames[i], name, len) == 0) return (int)i;
  }
  return -1;
}

static RolltuiStyleColorRaw color_toward(RolltuiStyleColorRaw c, RolltuiStyleColorRaw ground, double keep) {
  if (c.kind != 2 /* Rgb */ || ground.kind != 2) return c;
  c.r = (unsigned char)(ground.r + (c.r - ground.r) * keep + 0.5);
  c.g = (unsigned char)(ground.g + (c.g - ground.g) * keep + 0.5);
  c.b = (unsigned char)(ground.b + (c.b - ground.b) * keep + 0.5);
  return c;
}
void rolltui_style_fade(const RolltuiStyle* st, RolltuiStyleColorRaw ground, double keep, RolltuiStyle* out) {
  RolltuiStyle s = *st;
  if (keep < 0) keep = 0;
  if (keep > 1) keep = 1;
  s.fg = color_toward(s.fg, ground, keep);
  s.bg = color_toward(s.bg, ground, keep);
  *out = s;
}

void rolltui_style_on(RolltuiStyle* st, RolltuiStyleColorRaw ground) {
  if (st->fg.kind == 0) st->fg = ground;
  if (st->bg.kind == 0) st->bg = ground;
}
