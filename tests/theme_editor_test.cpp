//
// theme_editor_test.cpp — the theme editor's model (milestone 14): every role is
// reachable (asserted against kRoleCount), the three-level navigation Roles › role › fg
// › palette entry, live preview while the selection moves, Enter commits, Escape cancels
// back to the committed value, toggles commit at once, a custom colour typed and
// committed (and one refused), Ctrl-Z / Ctrl-Y over whole snapshots, mode switching
// editing the other variant, and the written-back pair object round-tripping through
// the theme loader. Also the UndoStack itself.
//
#include <string>

#include "../tools/undo_stack.hpp"
#include "rolltui/c/rolltui_json.h"
#include "rolltui_test.hpp"
#include "theme_editor.hpp"

using namespace rolltui;
using namespace rolltui::tools;
using namespace rolltui_test;

namespace {
KeyEvent key(Key k) { KeyEvent e; e.key = k; return e; }
KeyEvent ch(char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); return e; }
KeyEvent ctrl(char c) { KeyEvent e = ch(c); e.ctrl = true; return e; }
void type(ThemeEditor& ed, const std::string& s) { for (char c : s) ed.handle(ch(c)); }
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
  ThemeLoadReport rep;
  check(ed.load(*ThemePresets::shipped("default"), rep) && rep.clean(), "loads the shipped default (both variants)");
  const Theme base_dark = *builtin_theme("default-dark");
  check(ed.current().styles == base_dark.styles && ed.mode() == ThemeMode::Dark, "edits and previews the dark variant first");

  // ---- every role reachable ----
  {
    const MenuItem* roles = ed.menu().find("roles");
    check(roles && roles->children.size() == kRoleCount, "the Roles level lists every role (" + std::to_string(roles ? roles->children.size() : 0) + " of " + std::to_string(kRoleCount) + ")");
    bool all = true;
    for (std::size_t i = 0; i < kRoleCount; ++i) {
      const std::string base = "role." + std::string(kRoleNames[i]);
      all &= ed.menu().find(base) && ed.menu().find(base + ".fg") && ed.menu().find(base + ".bg") && ed.menu().find(base + ".bold") && ed.menu().find(base + ".fg.custom");
    }
    check(all, "every role has fg, bg, custom fg and the attribute toggles");
    check(ed.menu().find("role.md_heading.fg")->value == color_to_string(base_dark.style(Role::md_heading).fg), "a choice shows the role's current colour as its value");
    check(ed.palette().size() > 5 && ed.palette()[0].id == "none", "the palette is every colour in use, none first (" + std::to_string(ed.palette().size()) + " entries)");
  }
  // ---- three levels deep: Roles › md_heading › fg › entry; preview, cancel, commit ----
  const Color original = base_dark.style(Role::md_heading).fg;
  {
    ed.handle(key(Key::Enter));  // Roles
    type(ed, "md_head");
    ThemeEditor::Outcome o = ed.handle(key(Key::Enter));  // md_heading
    check(ed.menu().breadcrumb() == "theme editor \xE2\x80\xBA Roles \xE2\x80\xBA md_heading" && ed.focused_role() == Role::md_heading,
          "filter + Enter reaches md_heading; the editor knows the focused role [" + ed.menu().breadcrumb() + "]");
    o = ed.handle(key(Key::Enter));  // fg choice
    check(ed.menu().level().id == "role.md_heading.fg" && ed.menu().selected_item()->id == color_to_string(original), "the fg choice opens on the current colour");
    // Move to a different entry: the preview applies live.
    Color previewed = original;
    for (int i = 0; i < 6 && previewed == original; ++i) {
      o = ed.handle(key(Key::Down));
      previewed = *parse_color(ed.menu().selected_item()->id);
    }
    check(previewed != original && ed.current().style(Role::md_heading).fg == previewed && ed.previewing() && o.kind == ThemeEditor::Outcome::Kind::Changed,
          "moving the selection previews that colour on the role before anything is committed");
    check(ed.highlighted_color() == previewed && ed.status_line().find("previewing") == 0, "the highlighted colour and the status say so");
    check(ed.committed().dark.style(Role::md_heading).fg == original && ed.undo_depth() == 0, "…and nothing is committed yet");
    o = ed.handle(key(Key::Escape));
    check(!ed.previewing() && ed.current().style(Role::md_heading).fg == original && ed.menu().level().id == "role.md_heading",
          "Escape cancels: the field returns to its committed value and the menu ascends");
    ed.handle(key(Key::Enter));  // fg again
    for (int i = 0; i < 6 && *parse_color(ed.menu().selected_item()->id) == original; ++i) ed.handle(key(Key::Down));
    const Color chosen = *parse_color(ed.menu().selected_item()->id);
    o = ed.handle(key(Key::Enter));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && ed.committed().dark.style(Role::md_heading).fg == chosen && ed.undo_depth() == 1 && !ed.previewing(),
          "Enter commits: the committed theme has the colour, undo depth 1");
    check(ed.menu().find("role.md_heading.fg")->value == color_to_string(chosen), "…and the choice shows the new value");
    check(ed.committed().light.style(Role::md_heading).fg == builtin_theme("default-light")->style(Role::md_heading).fg, "the light variant is untouched");
    // Undo / redo by key.
    o = ed.handle(ctrl('z'));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && ed.current().style(Role::md_heading).fg == original && ed.undo_depth() == 0 && ed.redo_depth() == 1,
          "Ctrl-Z puts the original back — and reports Committed, so the host writes the undone value to the store");
    ed.handle(ctrl('y'));
    check(ed.current().style(Role::md_heading).fg == chosen && ed.redo_depth() == 0, "Ctrl-Y re-applies it");
  }
  // ---- toggles commit at once ----
  {
    ed.handle(key(Key::Down));
    ed.handle(key(Key::Down));
    ed.handle(key(Key::Down));
    ed.handle(key(Key::Down));  // bold (fg, bg, custom fg, custom bg, bold)
    check(ed.menu().selected_item()->id == "role.md_heading.bold", "the fifth field is the bold toggle [" + ed.menu().selected_item()->id + "]");
    const bool was = ed.current().style(Role::md_heading).bold;
    ThemeEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && ed.committed().dark.style(Role::md_heading).bold == !was && ed.undo_depth() == 2, "Enter on a toggle flips and commits");
  }
  // ---- a custom colour: typed live, committed; a bad one refused ----
  {
    ed.handle(key(Key::Up));
    ed.handle(key(Key::Up));  // custom fg
    check(ed.menu().selected_item()->id == "role.md_heading.fg.custom", "custom fg [" + ed.menu().selected_item()->id + "]");
    ed.handle(key(Key::Enter));
    type(ed, "#123456");
    check(ed.previewing() && ed.current().style(Role::md_heading).fg == Color::rgb(0x12, 0x34, 0x56), "typing a valid colour previews it live");
    ThemeEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && ed.committed().dark.style(Role::md_heading).fg == Color::rgb(0x12, 0x34, 0x56), "Enter commits the custom colour");
    bool in_palette = false;
    for (const PaletteEntry& p : ed.palette()) in_palette |= p.id == "#123456";
    check(in_palette && ed.menu().find("role.text.fg")->children.size() == ed.palette().size(), "…and it joins the palette offered to every role");
    ed.handle(key(Key::Enter));
    type(ed, "orange");
    // Prefix validity, one key at a time: o, r, a refused (no colour starts so), n
    // accepted (a prefix of "none"), g and e refused ("ng", "ne" begin no colour) — the
    // text is "n".
    check(ed.menu().editing() && ed.menu().editing_text() == "n" && ed.status_line().find("refused: not the start of a colour") != std::string::npos,
          "keys that cannot begin a colour are refused at the keystroke with the reason [" + ed.menu().editing_text() + " | " + ed.status_line() + "]");
    o = ed.handle(key(Key::Enter));
    check(o.kind == ThemeEditor::Outcome::Kind::Changed && ed.menu().editing() && ed.current().style(Role::md_heading).fg == Color::rgb(0x12, 0x34, 0x56) &&
              ed.status_line().find("not a colour yet") != std::string::npos,
          "Enter on a text that is not yet a colour is refused with the reason; the field keeps its committed value");
    ed.handle(key(Key::Escape));
    check(!ed.menu().editing() && ed.committed().dark.style(Role::md_heading).fg == Color::rgb(0x12, 0x34, 0x56), "Escape leaves the committed colour");
  }
  // ---- mode switch edits the other variant ----
  {
    ed.handle(key(Key::Escape));
    ed.handle(key(Key::Escape));  // top
    ed.handle(key(Key::Down));   // Mode
    ed.handle(key(Key::Enter));
    ed.handle(key(Key::Down));   // light
    ed.handle(key(Key::Enter));
    check(ed.mode() == ThemeMode::Light && ed.current().styles == ed.committed().light.styles, "choosing light previews and edits the light variant");
    check(ed.current().style(Role::md_heading).fg == builtin_theme("default-light")->style(Role::md_heading).fg, "the light variant has its own heading colour");
    RolltuiJsonValue* pair = ed.colours_json("edited");  // OWNED — freed below
    ThemeLoadReport r2;
    std::optional<Theme> d = load_theme(pair, ThemeMode::Dark, r2), l = load_theme(pair, ThemeMode::Light, r2);
    check(d && l && d->styles == ed.committed().dark.styles && l->styles == ed.committed().light.styles,
          "the written-back pair object loads to both variants exactly (bold was set in dark only: an attribute pair)");
    const RolltuiJsonValue* bold = rolltui_json_get(rolltui_json_get(rolltui_json_get(pair, "roles", 5), "md_heading", 10), "bold", 4);
    check(rolltui_json_is_object(bold), "…the file carries bold as a {dark, light} pair for that role");
    rolltui_json_free(pair);
  }
  // ---- the host-facing outcomes ----
  {
    ed.set_presets({"default", "mono", "mine"});
    ed.set_shipped({"default", "mono"}, true);
    ed.handle(key(Key::Home));
    type(ed, "load");
    ed.handle(key(Key::Enter));
    ed.handle(key(Key::Down));
    ThemeEditor::Outcome o = ed.handle(key(Key::Enter));
    check(o == ThemeEditor::Outcome{ThemeEditor::Outcome::Kind::LoadPreset, "mono"}, "Load preset › mono asks the host to load");
    ed.handle(key(Key::Escape));  // clears the filter left by the last step
    ed.handle(key(Key::Home));
    type(ed, "save");
    ed.handle(key(Key::Enter));
    type(ed, "mine");
    o = ed.handle(key(Key::Enter));
    check(o == ThemeEditor::Outcome{ThemeEditor::Outcome::Kind::SaveAs, "mine"}, "Save as preset asks the host with the name");
    ed.handle(key(Key::Escape));
    ed.handle(key(Key::Home));
    type(ed, "shipped");
    ed.handle(key(Key::Enter));
    o = ed.handle(key(Key::Enter));
    check(o == ThemeEditor::Outcome{ThemeEditor::Outcome::Kind::WriteShipped, "default"}, "Write a shipped preset asks the host (with the privilege set)");
    ed.set_shipped({"default"}, false);
    check(!ed.menu().find("write_shipped")->enabled, "…and is disabled without it");
    ed.handle(key(Key::Escape));
    ed.handle(key(Key::Home));
    type(ed, "reset to the built");
    o = ed.handle(key(Key::Enter));
    check(o.kind == ThemeEditor::Outcome::Kind::ResetBuiltin, "Reset to built-in is an outcome the host confirms, never applied here");
    ed.handle(key(Key::Escape));
    o = ed.handle(key(Key::Escape));
    check(o.kind == ThemeEditor::Outcome::Kind::Closed, "Escape at the top asks the host to close the editor");
  }
  // ---- milestone 15: check, fixes, generate ----
  {
    ThemeEditor e2;
    ThemePreset broken = *ThemePresets::shipped("default");
    // Break the dark variant: a dim link and an identical diff pair.
    ThemeLoadReport r;
    e2.load(broken, r);
    check(e2.fixes().empty() && e2.badges_line().find("readable") != std::string::npos && e2.badges_line().find("cvd-safe") != std::string::npos,
          "the shipped default has nothing to fix and its badges read dark + readable + cvd-safe [" + e2.badges_line() + "]");
    check(e2.report().find("badges: dark") == 0, "report() is the analysis text");
    ThemeEdit bad = e2.committed();
    bad.dark.style(Role::md_link).fg = Color::rgb(0x30, 0x34, 0x3a);
    bad.dark.style(Role::diff_removed).fg = bad.dark.style(Role::diff_added).fg;
    e2.replace(bad);
    check(e2.fixes().size() == 2 && e2.menu().find("fixes")->children.size() == 2, "a broken variant lists its proposals under Fixes (" + std::to_string(e2.fixes().size()) + ")");
    type(e2, "check");
    ThemeEditor::Outcome o = e2.handle(key(Key::Enter));
    check(o.kind == ThemeEditor::Outcome::Kind::Check, "Check asks the host to show the report");
    e2.handle(key(Key::Escape));
    e2.handle(key(Key::Home));
    type(e2, "fixes");
    e2.handle(key(Key::Enter));  // Fixes level
    const std::string first(e2.menu().selected_item()->label.view());
    o = e2.handle(key(Key::Enter));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && e2.fixes().size() == 1 && e2.undo_depth() == 2,
          "Enter on a proposal applies it as a commit (undoable) and the list shrinks: applied [" + first + "]");
    e2.handle(ctrl('z'));
    check(e2.fixes().size() == 2, "Ctrl-Z brings the proposal back");
    e2.handle(key(Key::Home));
    e2.handle(key(Key::Escape));
    e2.handle(key(Key::Home));
    type(e2, "generate");
    e2.handle(key(Key::Enter));
    e2.handle(key(Key::Down));  // seed
    e2.handle(key(Key::Enter));
    e2.handle(key(Key::Backspace));
    type(e2, "7");
    e2.handle(key(Key::Enter));
    e2.handle(key(Key::Down));
    e2.handle(key(Key::Down));  // generate
    o = e2.handle(key(Key::Enter));
    check(o.kind == ThemeEditor::Outcome::Kind::Committed && e2.committed().dark.name == "gen-analogous-7-0.00" && e2.committed().light.name == "gen-analogous-7-0.00",
          "Generate replaces both variants with the seeded theme (dark and light grounds from one seed) [" + e2.committed().dark.name + "]");
    check(e2.status_line().find("generated gen-analogous-7") != std::string::npos && e2.badges_line().find("readable") != std::string::npos,
          "…the status names it and the generated variant is readable [" + e2.badges_line() + "]");
    check(e2.fixes().empty(), "…with nothing left to fix at chaos 0");
  }
  return report("rolltui theme_editor_test");
}
