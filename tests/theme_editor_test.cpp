//
// theme_editor_test.cpp — the theme editor's model (milestone 14): every role is
// reachable (asserted against ROLLTUI_ROLE_COUNT), the three-level navigation Roles ›
// role › fg › palette entry, live preview while the selection moves, Enter commits,
// Escape cancels back to the committed value, toggles commit at once, a custom colour
// typed and committed (and one refused), Ctrl-Z / Ctrl-Y over whole snapshots, mode
// switching editing the other variant, and the written-back pair object round-tripping
// through the theme loader. Also the UndoStack itself.
//
// Phase 17 m1d: drives the editor through `rolltui/c/*.h` directly — no `rolltui/*.hpp`.
//
#include <string>

#include "../tools/undo_stack.hpp"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui_test.hpp"
#include "theme_editor.hpp"

// PHASE 17 m3: the forward declaration of `rolltui::theme_vocab()` that stood here is gone with
// `Theme.cpp`. `rolltui_theme_default_vocab()` is the same table and always was — the C++ one
// only forwarded to it, which is why nothing but the spelling changes below.

using namespace rolltui::tools;
using namespace rolltui_test;

namespace {
RolltuiChord key(unsigned char k) {
  RolltuiChord e{};
  e.key = k;
  return e;
}
RolltuiChord ch(char c) {
  RolltuiChord e{};
  e.key = ROLLTUI_KEY_CHAR;
  e.ch = static_cast<char32_t>(c);
  return e;
}
RolltuiChord ctrl(char c) {
  RolltuiChord e = ch(c);
  e.ctrl = 1;
  return e;
}
ThemeEditor::Outcome handle(ThemeEditor& ed, const RolltuiChord& k) {
  const RolltuiEvent e{ROLLTUI_EVENT_KEY, k, {}, nullptr, 0};
  return ed.handle(&e);
}
void type(ThemeEditor& ed, const std::string& s) { for (char c : s) handle(ed, ch(c)); }
MenuItem* find(RolltuiMenu* m, std::string_view id) { return rolltui_menu_find(m, id.data(), id.size()); }
unsigned char role(const char* name) {
  const int r = rolltui_role_from_name(name, std::string_view(name).size());
  return r >= 0 ? static_cast<unsigned char>(r) : 0;
}
std::string role_name(unsigned char r) {
  std::size_t len = 0;
  const char* p = rolltui_role_name(r, &len);
  return p ? std::string(p, len) : std::string();
}
std::optional<Color> parse_color(std::string_view text) {
  Color c{};
  if (!rolltui_color_parse(text.data(), text.size(), &c)) return std::nullopt;
  return c;
}
std::string color_to_string(Color c) {
  char buf[ROLLTUI_COLOR_STRING_MAX];
  return std::string(buf, rolltui_color_to_string(c, buf, sizeof buf));
}
// rolltui::ThemeLoadReport::clean()'s shape (Theme.hpp), over the C report directly.
bool theme_report_clean(const RolltuiThemeReport& r) {
  return r.error.empty() && r.missing_roles_n == 0 && r.unknown_keys_n == 0 && r.bad_values_n == 0;
}
bool styles_equal(const RolltuiStyle* a, const RolltuiStyle* b) {
  for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i)
    if (!(a[i] == b[i])) return false;
  return true;
}
std::string breadcrumb_of(ThemeEditor& ed) {
  RolltuiStr s{};
  rolltui_menu_breadcrumb(ed.menu(), &s);
  const std::string out(s.p ? s.p : "", s.n);
  rolltui_str_free(&s);
  return out;
}
std::string editing_text_of(ThemeEditor& ed) {
  std::size_t len = 0;
  const char* p = rolltui_input_text(rolltui_menu_editor(ed.menu()), &len);
  return std::string(p, len);
}
// The shipped "default" theme preset's colours tree — OWNED, the caller frees it. The
// theme preset store (Presets.hpp/rolltui_presets.h's generic mechanics) is not this
// file's to stand up just to reach one embedded file; `rolltui_theme_load` reads
// "name"/"defs"/"roles" straight off whatever root it is given; the "mode"/"depth" keys
// a preset also carries are the preset store's own concern and rolltui_theme_load never
// looks at them, so parsing the shipped file directly is the same colours tree
// `ThemePreset::colours` would have held.
RolltuiJsonValue* shipped_default_colours() {
  const char* text = rolltui_embedded_text(rolltui_kThemePresets, rolltui_kThemePresetCount, "default", 7);
  RolltuiStr err{};
  RolltuiJsonValue* root = rolltui_json_parse(text, text ? std::string_view(text).size() : 0, &err);
  rolltui_str_free(&err);
  // The preset FILE wraps the theme object under "colours" (mode/depth are the preset
  // store's own siblings of it, not rolltui_theme_load's concern) — ThemePreset::colours
  // is exactly this sub-tree, cloned so it outlives `root`.
  RolltuiJsonValue* colours = rolltui_json_clone(rolltui_json_get(root, "colours", 7));
  rolltui_json_free(root);
  return colours;
}
}  // namespace

int main() {
  // ---- UndoStack ----
  {
    UndoStack<int> u(1);
    check(!u.can_undo() && !u.can_redo() && u.current() == 1, "a baseline: nothing to undo or redo");
    u.commit(2);
    u.commit(3);
    check(u.undo_depth() == 2 && u.current() == 3, "two commits: depth 2");
    check(u.undo() && u.current() == 2 && u.undo() && u.current() == 1 && !u.undo(), "undo walks back to the baseline and stops");
    check(u.redo() && u.current() == 2, "redo walks forward");
    u.commit(9);
    check(!u.can_redo() && u.current() == 9 && u.undo_depth() == 2, "a commit after an undo drops the redo branch");
    UndoStack<int> small(0, 3);
    for (int i = 1; i <= 5; ++i) small.commit(i);
    check(small.undo_depth() == 2 && small.current() == 5, "the limit bounds the history (3 kept of 6)");
  }
  ThemeEditor ed;
  RolltuiThemeReport rep{};
  RolltuiJsonValue* shipped = shipped_default_colours();
  check(ed.load(shipped, &rep) && theme_report_clean(rep), "loads the shipped default (both variants)");
  rolltui_theme_report_release(&rep);
  rolltui_json_free(shipped);
  std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> base_dark{};
  rolltui_effect_map_free(rolltui_theme_builtin_fill("default-dark", 12, base_dark.data(), ROLLTUI_ROLE_COUNT));
  check(styles_equal(ed.current(), base_dark.data()) && ed.mode() == ROLLTUI_MODE_DARK, "edits and previews the dark variant first");

  const unsigned char md_heading = role("md_heading");
  // ---- every role reachable ----
  {
    const MenuItem* roles = find(ed.menu(), "roles");
    check(roles && roles->children.size() == ROLLTUI_ROLE_COUNT, "the Roles level lists every role (" + std::to_string(roles ? roles->children.size() : 0) + " of " + std::to_string(ROLLTUI_ROLE_COUNT) + ")");
    bool all = true;
    for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i) {
      const std::string base = "role." + role_name(static_cast<unsigned char>(i));
      all &= find(ed.menu(), base) && find(ed.menu(), base + ".fg") && find(ed.menu(), base + ".bg") && find(ed.menu(), base + ".bold") && find(ed.menu(), base + ".fg.custom");
    }
    check(all, "every role has fg, bg, custom fg and the attribute toggles");
    check(view_of(find(ed.menu(), "role.md_heading.fg")->value) == color_to_string(base_dark[md_heading].fg), "a choice shows the role's current colour as its value");
    check(ed.palette().size() > 5 && ed.palette()[0].id == "none", "the palette is every colour in use, none first (" + std::to_string(ed.palette().size()) + " entries)");
  }
  // ---- three levels deep: Roles › md_heading › fg › entry; preview, cancel, commit ----
  const Color original = base_dark[md_heading].fg;
  {
    handle(ed, key(ROLLTUI_KEY_ENTER));  // Roles
    type(ed, "md_head");
    ThemeEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));  // md_heading
    check(breadcrumb_of(ed) == "theme editor \xE2\x80\xBA Roles \xE2\x80\xBA md_heading" && ed.focused_role() == md_heading,
          "filter + Enter reaches md_heading; the editor knows the focused role [" + breadcrumb_of(ed) + "]");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));  // fg choice
    check(rolltui_menu_level(ed.menu())->id == "role.md_heading.fg" && view_of(rolltui_menu_selected_item(ed.menu())->id) == color_to_string(original), "the fg choice opens on the current colour");
    // Move to a different entry: the preview applies live.
    Color previewed = original;
    for (int i = 0; i < 6 && previewed == original; ++i) {
      o = handle(ed, key(ROLLTUI_KEY_DOWN));
      previewed = *parse_color(view_of(rolltui_menu_selected_item(ed.menu())->id));
    }
    check(!(previewed == original) && ed.current()[md_heading].fg == previewed && ed.previewing() && o.kind == ThemeEditor::Outcome::Kind::Changed,
          "moving the selection previews that colour on the role before anything is committed");
    check(ed.highlighted_color() == previewed && ed.status_line().find("previewing") == 0, "the highlighted colour and the status say so");
    check(ed.committed().dark[md_heading].fg == original && ed.undo_depth() == 0, "…and nothing is committed yet");
    o = handle(ed, key(ROLLTUI_KEY_ESCAPE));
    check(!ed.previewing() && ed.current()[md_heading].fg == original && rolltui_menu_level(ed.menu())->id == "role.md_heading",
          "Escape cancels: the field returns to its committed value and the menu ascends");
    handle(ed, key(ROLLTUI_KEY_ENTER));  // fg again
    for (int i = 0; i < 6 && *parse_color(view_of(rolltui_menu_selected_item(ed.menu())->id)) == original; ++i) handle(ed, key(ROLLTUI_KEY_DOWN));
    const Color chosen = *parse_color(view_of(rolltui_menu_selected_item(ed.menu())->id));
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && ed.committed().dark[md_heading].fg == chosen && ed.undo_depth() == 1 && !ed.previewing(),
          "Enter commits: the committed theme has the colour, undo depth 1");
    check(view_of(find(ed.menu(), "role.md_heading.fg")->value) == color_to_string(chosen), "…and the choice shows the new value");
    std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> base_light{};
    rolltui_effect_map_free(rolltui_theme_builtin_fill("default-light", 13, base_light.data(), ROLLTUI_ROLE_COUNT));
    check(ed.committed().light[md_heading].fg == base_light[md_heading].fg, "the light variant is untouched");
    // Undo / redo by key.
    o = handle(ed, ctrl('z'));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && ed.current()[md_heading].fg == original && ed.undo_depth() == 0 && ed.redo_depth() == 1,
          "Ctrl-Z puts the original back — and reports Committed, so the host writes the undone value to the store");
    handle(ed, ctrl('y'));
    check(ed.current()[md_heading].fg == chosen && ed.redo_depth() == 0, "Ctrl-Y re-applies it");
  }
  // ---- toggles commit at once ----
  {
    handle(ed, key(ROLLTUI_KEY_DOWN));
    handle(ed, key(ROLLTUI_KEY_DOWN));
    handle(ed, key(ROLLTUI_KEY_DOWN));
    handle(ed, key(ROLLTUI_KEY_DOWN));  // bold (fg, bg, custom fg, custom bg, bold)
    check(rolltui_menu_selected_item(ed.menu())->id == "role.md_heading.bold", "the fifth field is the bold toggle [" + str_of(rolltui_menu_selected_item(ed.menu())->id) + "]");
    const bool was = ed.current()[md_heading].bold;
    ThemeEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && ed.committed().dark[md_heading].bold == !was && ed.undo_depth() == 2, "Enter on a toggle flips and commits");
  }
  // ---- a custom colour: typed live, committed; a bad one refused ----
  {
    handle(ed, key(ROLLTUI_KEY_UP));
    handle(ed, key(ROLLTUI_KEY_UP));  // custom fg
    check(rolltui_menu_selected_item(ed.menu())->id == "role.md_heading.fg.custom", "custom fg [" + str_of(rolltui_menu_selected_item(ed.menu())->id) + "]");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "#123456");
    check(ed.previewing() && ed.current()[md_heading].fg == Color::rgb(0x12, 0x34, 0x56), "typing a valid colour previews it live");
    ThemeEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && ed.committed().dark[md_heading].fg == Color::rgb(0x12, 0x34, 0x56), "Enter commits the custom colour");
    bool in_palette = false;
    for (const PaletteEntry& p : ed.palette()) in_palette |= p.id == "#123456";
    check(in_palette && find(ed.menu(), "role.text.fg")->children.size() == ed.palette().size(), "…and it joins the palette offered to every role");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "orange");
    // Prefix validity, one key at a time: o, r, a refused (no colour starts so), n
    // accepted (a prefix of "none"), g and e refused ("ng", "ne" begin no colour) — the
    // text is "n".
    check(rolltui_menu_editing(ed.menu()) && editing_text_of(ed) == "n" && ed.status_line().find("refused: not the start of a colour") != std::string::npos,
          "keys that cannot begin a colour are refused at the keystroke with the reason [" + editing_text_of(ed) + " | " + ed.status_line() + "]");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == ThemeEditor::Outcome::Kind::Changed && rolltui_menu_editing(ed.menu()) && ed.current()[md_heading].fg == Color::rgb(0x12, 0x34, 0x56) &&
              ed.status_line().find("not a colour yet") != std::string::npos,
          "Enter on a text that is not yet a colour is refused with the reason; the field keeps its committed value");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    check(!rolltui_menu_editing(ed.menu()) && ed.committed().dark[md_heading].fg == Color::rgb(0x12, 0x34, 0x56), "Escape leaves the committed colour");
  }
  // ---- mode switch edits the other variant ----
  {
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    handle(ed, key(ROLLTUI_KEY_ESCAPE));  // top
    handle(ed, key(ROLLTUI_KEY_DOWN));    // Mode
    handle(ed, key(ROLLTUI_KEY_ENTER));
    handle(ed, key(ROLLTUI_KEY_DOWN));    // light
    handle(ed, key(ROLLTUI_KEY_ENTER));
    check(ed.mode() == ROLLTUI_MODE_LIGHT && styles_equal(ed.current(), ed.committed().light.data()), "choosing light previews and edits the light variant");
    std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> base_light{};
    rolltui_effect_map_free(rolltui_theme_builtin_fill("default-light", 13, base_light.data(), ROLLTUI_ROLE_COUNT));
    check(ed.current()[md_heading].fg == base_light[md_heading].fg, "the light variant has its own heading colour");
    RolltuiJsonValue* pair = ed.colours_json("edited");  // OWNED — freed below
    RolltuiThemeReport r2{};
    std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> d{}, l{};
    RolltuiStr d_name{}, l_name{};
    RolltuiEffectMap* d_eff = rolltui_theme_load(pair, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), d.data(), &d_name, &r2);
    RolltuiThemeReport r3{};
    RolltuiEffectMap* l_eff = rolltui_theme_load(pair, ROLLTUI_MODE_LIGHT, rolltui_theme_default_vocab(), l.data(), &l_name, &r3);
    check(d_eff && l_eff && styles_equal(d.data(), ed.committed().dark.data()) && styles_equal(l.data(), ed.committed().light.data()),
          "the written-back pair object loads to both variants exactly (bold was set in dark only: an attribute pair)");
    rolltui_str_free(&d_name);
    rolltui_str_free(&l_name);
    rolltui_effect_map_free(d_eff);
    rolltui_effect_map_free(l_eff);
    rolltui_theme_report_release(&r2);
    rolltui_theme_report_release(&r3);
    const RolltuiJsonValue* bold = rolltui_json_get(rolltui_json_get(rolltui_json_get(pair, "roles", 5), "md_heading", 10), "bold", 4);
    check(rolltui_json_is_object(bold), "…the file carries bold as a {dark, light} pair for that role");
    rolltui_json_free(pair);
  }
  // ---- the host-facing outcomes ----
  {
    ed.set_presets({"default", "mono", "mine"});
    ed.set_shipped({"default", "mono"}, true);
    handle(ed, key(ROLLTUI_KEY_HOME));
    type(ed, "load");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    handle(ed, key(ROLLTUI_KEY_DOWN));
    ThemeEditor::Outcome o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o == ThemeEditor::Outcome{ThemeEditor::Outcome::Kind::LoadPreset, "mono"}, "Load preset › mono asks the host to load");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));  // clears the filter left by the last step
    handle(ed, key(ROLLTUI_KEY_HOME));
    type(ed, "save");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    type(ed, "mine");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o == ThemeEditor::Outcome{ThemeEditor::Outcome::Kind::SaveAs, "mine"}, "Save as preset asks the host with the name");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    handle(ed, key(ROLLTUI_KEY_HOME));
    type(ed, "shipped");
    handle(ed, key(ROLLTUI_KEY_ENTER));
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o == ThemeEditor::Outcome{ThemeEditor::Outcome::Kind::WriteShipped, "default"}, "Write a shipped preset asks the host (with the privilege set)");
    ed.set_shipped({"default"}, false);
    check(!find(ed.menu(), "write_shipped")->enabled, "…and is disabled without it");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    handle(ed, key(ROLLTUI_KEY_HOME));
    type(ed, "reset to the built");
    o = handle(ed, key(ROLLTUI_KEY_ENTER));
    check(o.kind == ThemeEditor::Outcome::Kind::ResetBuiltin, "Reset to built-in is an outcome the host confirms, never applied here");
    handle(ed, key(ROLLTUI_KEY_ESCAPE));
    o = handle(ed, key(ROLLTUI_KEY_ESCAPE));
    check(o.kind == ThemeEditor::Outcome::Kind::Closed, "Escape at the top asks the host to close the editor");
  }
  // ---- milestone 15: check, fixes, generate ----
  {
    ThemeEditor e2;
    RolltuiThemeReport r{};
    RolltuiJsonValue* broken_shipped = shipped_default_colours();
    e2.load(broken_shipped, &r);
    rolltui_theme_report_release(&r);
    rolltui_json_free(broken_shipped);
    check(e2.fixes().empty() && e2.badges_line().find("readable") != std::string::npos && e2.badges_line().find("cvd-safe") != std::string::npos,
          "the shipped default has nothing to fix and its badges read dark + readable + cvd-safe [" + e2.badges_line() + "]");
    check(e2.report().find("badges: dark") == 0, "report() is the analysis text");
    ThemeEdit bad = e2.committed();
    const unsigned char md_link = role("md_link"), diff_removed = role("diff_removed"), diff_added = role("diff_added");
    bad.dark[md_link].fg = Color::rgb(0x30, 0x34, 0x3a);
    bad.dark[diff_removed].fg = bad.dark[diff_added].fg;
    e2.replace(bad);
    check(e2.fixes().size() == 2 && find(e2.menu(), "fixes")->children.size() == 2, "a broken variant lists its proposals under Fixes (" + std::to_string(e2.fixes().size()) + ")");
    type(e2, "check");
    ThemeEditor::Outcome o = handle(e2, key(ROLLTUI_KEY_ENTER));
    check(o.kind == ThemeEditor::Outcome::Kind::Check, "Check asks the host to show the report");
    handle(e2, key(ROLLTUI_KEY_ESCAPE));
    handle(e2, key(ROLLTUI_KEY_HOME));
    type(e2, "fixes");
    handle(e2, key(ROLLTUI_KEY_ENTER));  // Fixes level
    const std::string first(view_of(rolltui_menu_selected_item(e2.menu())->label));
    o = handle(e2, key(ROLLTUI_KEY_ENTER));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && e2.fixes().size() == 1 && e2.undo_depth() == 2,
          "Enter on a proposal applies it as a commit (undoable) and the list shrinks: applied [" + first + "]");
    handle(e2, ctrl('z'));
    check(e2.fixes().size() == 2, "Ctrl-Z brings the proposal back");
    handle(e2, key(ROLLTUI_KEY_HOME));
    handle(e2, key(ROLLTUI_KEY_ESCAPE));
    handle(e2, key(ROLLTUI_KEY_HOME));
    type(e2, "generate");
    handle(e2, key(ROLLTUI_KEY_ENTER));
    handle(e2, key(ROLLTUI_KEY_DOWN));  // seed
    handle(e2, key(ROLLTUI_KEY_ENTER));
    handle(e2, key(ROLLTUI_KEY_BACKSPACE));
    type(e2, "7");
    handle(e2, key(ROLLTUI_KEY_ENTER));
    handle(e2, key(ROLLTUI_KEY_DOWN));
    handle(e2, key(ROLLTUI_KEY_DOWN));  // generate
    o = handle(e2, key(ROLLTUI_KEY_ENTER));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && e2.committed().dark_name == "gen-analogous-7-0.00" && e2.committed().light_name == "gen-analogous-7-0.00",
          "Generate replaces both variants with the seeded theme (dark and light grounds from one seed) [" + e2.committed().dark_name + "]");
    check(e2.status_line().find("generated gen-analogous-7") != std::string::npos && e2.badges_line().find("readable") != std::string::npos,
          "…the status names it and the generated variant is readable [" + e2.badges_line() + "]");
    check(e2.fixes().empty(), "…with nothing left to fix at chaos 0");
    // A generated theme's provenance and claimed badges are "meta" (Theme.cpp's own
    // shape); ThemeEdit carries it now (dark_meta/light_meta) specifically so a save does
    // not lose it — the loss the first attempt at this milestone made a different way.
    {
      RolltuiJsonValue* saved = e2.colours_json("generated");
      const RolltuiJsonValue* meta = rolltui_json_get(saved, "meta", 4);
      const RolltuiJsonValue* generator = rolltui_json_get(meta, "generator", 9);
      const double seed_num = rolltui_json_as_number(rolltui_json_get(generator, "seed", 4), -1);
      const RolltuiJsonValue* badges = rolltui_json_get(meta, "badges", 6);
      // An array when the two variants' claimed badges match, a {"dark","light"} pair when
      // they differ (theme_pair_to_json_value's own rule, carried verbatim) — either way,
      // present is the claim; chaos 0 with independent dark/light draws usually differs.
      check(rolltui_json_is_object(meta) && seed_num == 7 && (rolltui_json_is_array(badges) || rolltui_json_is_object(badges)),
            "the saved file carries the generated theme's provenance (seed 7) and its claimed badges, not just its colours");
      rolltui_json_free(saved);
    }
  }
  return report("rolltui theme_editor_test");
}
