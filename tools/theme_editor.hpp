#pragma once
//
// rolltui/tools/theme_editor.hpp — the theme editor (plan/phase-9.md, milestone 14,
// formerly 11b): the editor's MODEL, with no terminal in it, so the studio hosts
// it and a test drives it. It is a rolltui::Menu over the theme — Roles › <role> › fg
// › <palette entry> is three levels of the same navigation the settings menu uses —
// plus the recovery model the user set: every change applies LIVE to the preview as
// the selection moves or the text is typed; Enter COMMITS, Escape (or Left) CANCELS the
// focused change and the field returns to its committed value; Ctrl-Z / Ctrl-Y undo and
// redo over whole-theme snapshots (undo_stack.hpp); a full reset is a menu action the
// host confirms in a popup, never a bare key. Undo and reset work blind, which is what
// makes an editor drawn in the theme it is editing safe: an unreadable choice is felt
// at once and undone without seeing.
//
// The editor edits BOTH variants of a Theme preset's colours (dark and light, resolved
// from the preset's colours object), one at a time — the Mode choice picks which is
// previewed and edited — and writes them back as one object with {"dark","light"} pairs
// where they differ (theme_pair_to_json_value), so a preset that adapts to the terminal
// keeps adapting. A preset's `defs` palette, when it has one, names the palette
// entries; the palette is otherwise every distinct colour the theme uses, plus "none",
// plus a custom entry (#rrggbb, an index 0-255, or none) typed into an Input.
//
// The host's obligations, returned as Outcomes: re-render on Changed; write the
// committed colours into the preset store on Committed; run a save-as, a load, a
// shipped write or a reset (the last three after confirming) on the matching Outcome.
//
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Menu.hpp"
#include "rolltui/Presets.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/ThemeAnalysis.hpp"
#include "rolltui/ThemeGen.hpp"
#include "undo_stack.hpp"
#include "tool_actions.hpp"

namespace rolltui::tools {

struct ThemeEdit {
  Theme dark, light;
  bool operator==(const ThemeEdit& o) const { return dark.styles == o.dark.styles && light.styles == o.light.styles; }
};

struct PaletteEntry {
  Color color;
  std::string id;     // color_to_string
  std::string label;  // "#6ca0e0" or "blue = #6ca0e0" when a defs name exists
};

class ThemeEditor {
 public:
  struct Outcome {
    enum class Kind { None, Changed, Committed, SaveAs, WriteShipped, LoadPreset, ResetLoaded, ResetBuiltin, Check, Closed };
    Kind kind = Kind::None;
    std::string value;  // SaveAs: the name; WriteShipped: the shipped name; LoadPreset: the preset name
    bool operator==(const Outcome&) const = default;
  };

  ThemeEditor();

  // Loads a preset's colours (both variants) as the baseline; the undo stack restarts.
  // `report` receives the colours loader's problems. Returns false when the colours
  // are unusable (the editor keeps what it had).
  bool load(const ThemePreset& preset, ThemeLoadReport& report);
  void set_presets(std::vector<std::string> names);          // the Load choice's options
  void set_shipped(std::vector<std::string> names, bool may_write);  // the Write-shipped choice
  void set_mode(ThemeMode m);
  ThemeMode mode() const { return mode_; }

  // The theme being previewed right now (committed + any live, uncommitted change).
  const Theme& current() const { return mode_ == ThemeMode::Dark ? current_.dark : current_.light; }
  const ThemeEdit& current_edit() const { return current_; }
  const ThemeEdit& committed() const { return undo_.current(); }
  bool previewing() const { return preview_.has_value(); }
  // The committed variants as one file object. OWNED — the caller frees it with
  // `rolltui_json_free` (or hands it straight to `ThemePresets::set_colours`, which adopts
  // it: `store->set_colours(teditor.colours_json(store->origin()))`).
  RolltuiJsonValue* colours_json(std::string_view name) const;

  Menu& menu() { return menu_; }
  const Menu& menu() const { return menu_; }
  const std::vector<PaletteEntry>& palette() const { return palette_; }

  // Events already routed to the editor's window. Ctrl-Z / Ctrl-Y are handled here.
  Outcome handle(const Event& e, const Bindings& nav);  // `nav`: the host's bindings (menu + editor scopes)
  Outcome handle(const Event& e) { return handle(e, editor_bindings()); }  // tool_actions.hpp: the shipped table with the editors mounted
  bool undo();
  bool redo();
  std::size_t undo_depth() const { return undo_.undo_depth(); }
  std::size_t redo_depth() const { return undo_.redo_depth(); }
  // Puts a whole theme edit in as the new committed value (a reset, a generated theme).
  void replace(ThemeEdit e);

  // For the host's sample box: the role the menu is on (nullopt at the top levels), the
  // colour highlighted in a palette choice, a one-line status, and the badges the
  // analysis (milestone 15) computes for the variant being edited.
  std::optional<Role> focused_role() const;
  std::optional<Color> highlighted_color() const;
  std::string status_line() const;
  std::string badges_line() const;   // "badges: dark readable cvd-safe" (computed, never declared)
  std::string report() const;        // report_text(analyse(current()))
  const std::vector<Fix>& fixes() const { return fixes_; }  // the Fixes level's proposals

 private:
  struct Field { Role role; std::string name; };  // "fg" | "bg" | attribute
  static std::optional<Field> field_of(std::string_view id);
  void rebuild_menu();
  void rebuild_palette();
  void sync_values();
  void apply(Field f, Color c);
  void refresh_fixes();
  void begin_preview();
  void cancel_preview();
  Outcome commit_current();
  Style& style_at(Role r) { return (mode_ == ThemeMode::Dark ? current_.dark : current_.light).style(r); }

  Menu menu_;
  ThemeEdit current_;
  UndoStack<ThemeEdit> undo_;
  std::optional<ThemeEdit> preview_;  // the committed value while a live change is shown
  std::vector<PaletteEntry> palette_;
  std::vector<Fix> fixes_;
  std::vector<std::string> presets_, shipped_;
  bool may_write_shipped_ = false;
  ThemeMode mode_ = ThemeMode::Dark;
  json::Value defs_;  // the loaded preset's defs, for palette labels
  std::string last_level_;  // the level id after the previous event (to notice an ascend)
  std::string status_;
};

}  // namespace rolltui::tools
