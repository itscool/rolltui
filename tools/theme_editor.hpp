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
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_effects.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_menu_tree.h"
#include "rolltui/c/rolltui_style.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_analysis.h"
#include "rolltui/c/rolltui_theme_gen.h"
#include "undo_stack.hpp"
#include "tool_actions.hpp"

// `rolltui::theme_vocab()` — the role/effect-state NAME TABLE every rolltui_theme_load/_dump/
// _analyse/_generate call needs (rolltui_theme.h's RolltuiThemeVocab) — is defined in
// Theme.cpp with EXTERNAL LINKAGE for exactly this: `Presets.cpp` already reaches it this same
// way (forward-declared, not duplicated) rather than rebuilding the table a second time. This
// file is the second consumer to do that on purpose rather than write a third.
namespace rolltui {
const RolltuiThemeVocab& theme_vocab();
}

namespace rolltui::tools {

// The C tree/style types, under the names every editor already writes them as.
using MenuItem = RolltuiMenuItem;
using InputSpec = RolltuiInputSpec;
using Style = RolltuiStyle;
using Color = RolltuiStyleColor;

// The two variants' STYLES, NAMES and META — the "editor's own edit buffer" the
// vocabulary-ownership rule permits: `rolltui::Theme` (name + meta + styles + effects) is
// not ported and is going away with Theme.hpp. META IS CARRIED, unlike effects below: it
// holds a generated theme's provenance ("generator": {ruleset, seed, chaos}) and its
// claimed badges, which `colours_json` writes back out and `studio --check`'s
// `check_claims` reads — dropping it silently loses both the moment a generated theme is
// saved. OWNED (a clone on copy, freed on destruction), the same shape `Theme::meta`
// itself uses (Theme.hpp's `MetaDeleter`) — both variants matter here, unlike effects,
// because `colours_json` pairs their "badges" when they differ, mirroring
// `theme_pair_to_json_value` (Theme.cpp) exactly.
// EFFECTS are not part of the undo-tracked value: nothing in this editor's menu ever
// touches them, they are never DIFFERENT from what was loaded (`rolltui_theme_dump` only
// ever reads `dark`'s: rolltui_theme.h's own note), so `ThemeEditor` owns the one live
// effect map as a plain member instead of asking every commit to clone one nothing changed.
struct ThemeEdit {
  std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> dark{}, light{};
  std::string dark_name, light_name;
  RolltuiJsonValue* dark_meta = nullptr;   // OWNED; nullptr when the theme claims none
  RolltuiJsonValue* light_meta = nullptr;  // OWNED; ditto

  ThemeEdit() = default;
  // ADOPTS dark_meta/light_meta (ownership transfer) — pass a clone when the caller's own
  // copy must survive the call (rolltui_json_clone), or a freshly-owned one (a just-loaded
  // or just-generated theme's).
  ThemeEdit(std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> d, std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> l,
            std::string dn, std::string ln, RolltuiJsonValue* dm = nullptr, RolltuiJsonValue* lm = nullptr)
      : dark(d), light(l), dark_name(std::move(dn)), light_name(std::move(ln)), dark_meta(dm), light_meta(lm) {}
  ThemeEdit(const ThemeEdit& o)
      : dark(o.dark), light(o.light), dark_name(o.dark_name), light_name(o.light_name),
        dark_meta(rolltui_json_clone(o.dark_meta)), light_meta(rolltui_json_clone(o.light_meta)) {}
  ThemeEdit(ThemeEdit&& o) noexcept
      : dark(o.dark), light(o.light), dark_name(std::move(o.dark_name)), light_name(std::move(o.light_name)),
        dark_meta(o.dark_meta), light_meta(o.light_meta) {
    o.dark_meta = nullptr;
    o.light_meta = nullptr;
  }
  ThemeEdit& operator=(const ThemeEdit& o) {
    if (this != &o) {
      dark = o.dark;
      light = o.light;
      dark_name = o.dark_name;
      light_name = o.light_name;
      rolltui_json_free(dark_meta);
      dark_meta = rolltui_json_clone(o.dark_meta);
      rolltui_json_free(light_meta);
      light_meta = rolltui_json_clone(o.light_meta);
    }
    return *this;
  }
  ThemeEdit& operator=(ThemeEdit&& o) noexcept {
    if (this != &o) {
      dark = o.dark;
      light = o.light;
      dark_name = std::move(o.dark_name);
      light_name = std::move(o.light_name);
      rolltui_json_free(dark_meta);
      dark_meta = o.dark_meta;
      o.dark_meta = nullptr;
      rolltui_json_free(light_meta);
      light_meta = o.light_meta;
      o.light_meta = nullptr;
    }
    return *this;
  }
  ~ThemeEdit() {
    rolltui_json_free(dark_meta);
    rolltui_json_free(light_meta);
  }

  bool operator==(const ThemeEdit& o) const { return dark == o.dark && light == o.light; }
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
  ~ThemeEditor() {
    rolltui_menu_free(menu_);
    rolltui_effect_map_free(dark_effects_);
    rolltui_json_free(defs_);
  }
  ThemeEditor(const ThemeEditor&) = delete;
  ThemeEditor& operator=(const ThemeEditor&) = delete;

  // Loads a preset's colours (both variants) as the baseline; the undo stack restarts.
  // `colours` is the preset's parsed "colours" tree (ThemePreset::colours, a
  // RolltuiJsonValue* since Phase 17 m2); `report` receives the colours loader's
  // problems. Returns false when the colours are unusable (the editor keeps what it had).
  bool load(const RolltuiJsonValue* colours, RolltuiThemeReport* report);
  void set_presets(std::vector<std::string> names);          // the Load choice's options
  void set_shipped(std::vector<std::string> names, bool may_write);  // the Write-shipped choice
  void set_mode(unsigned char m);  // ROLLTUI_MODE_DARK | ROLLTUI_MODE_LIGHT
  unsigned char mode() const { return mode_; }

  // The theme being previewed right now (committed + any live, uncommitted change) —
  // a BORROW of this editor's own storage, valid until the next edit.
  const RolltuiStyle* current() const { return (mode_ == ROLLTUI_MODE_DARK ? current_.dark : current_.light).data(); }
  const ThemeEdit& current_edit() const { return current_; }
  const ThemeEdit& committed() const { return undo_.current(); }
  bool previewing() const { return preview_.has_value(); }
  // The committed variants as one file object. OWNED — the caller frees it with
  // `rolltui_json_free` (or hands it straight to `ThemePresets::set_colours`, which adopts
  // it: `store->set_colours(teditor.colours_json(store->origin()))`).
  RolltuiJsonValue* colours_json(std::string_view name) const;

  RolltuiMenu* menu() { return menu_; }
  const RolltuiMenu* menu() const { return menu_; }
  const std::vector<PaletteEntry>& palette() const { return palette_; }

  // Events already routed to the editor's window. Ctrl-Z / Ctrl-Y are handled here.
  Outcome handle(const RolltuiEvent* e, const RolltuiBindings* nav);  // `nav`: the host's bindings (menu + editor scopes)
  Outcome handle(const RolltuiEvent* e) { return handle(e, editor_bindings()); }  // tool_actions.hpp: the shipped table with the editors mounted
  bool undo();
  bool redo();
  std::size_t undo_depth() const { return undo_.undo_depth(); }
  std::size_t redo_depth() const { return undo_.redo_depth(); }
  // Puts a whole theme edit in as the new committed value (a reset, a generated theme).
  // `dark_effects` is ADOPTED (owned by this call): pass a clone when the caller's own
  // copy must survive it (rolltui_effect_map_clone), or a freshly-owned one (a
  // just-loaded theme's). nullptr keeps whatever effects this editor already has —
  // "generate" wants that, since a generated theme has none of its own to replace them with.
  void replace(ThemeEdit e, RolltuiEffectMap* dark_effects = nullptr);

  // For the host's sample box: the role the menu is on (nullopt at the top levels), the
  // colour highlighted in a palette choice, a one-line status, and the badges the
  // analysis (milestone 15) computes for the variant being edited.
  std::optional<unsigned char> focused_role() const;
  std::optional<Color> highlighted_color() const;
  std::string status_line() const;
  std::string badges_line() const;   // "badges: dark readable cvd-safe" (computed, never declared)
  std::string report() const;        // report_text(analyse(current()))
  const std::vector<RolltuiFix>& fixes() const { return fixes_; }  // the Fixes level's proposals

 private:
  struct Field { unsigned char role; std::string name; };  // "fg" | "bg" | attribute
  static std::optional<Field> field_of(std::string_view id);
  void rebuild_menu();
  void rebuild_palette();
  void sync_values();
  void apply(Field f, Color c);
  void refresh_fixes();
  void begin_preview();
  void cancel_preview();
  Outcome commit_current();
  RolltuiStyle& style_at(unsigned char r) { return (mode_ == ROLLTUI_MODE_DARK ? current_.dark : current_.light)[r]; }

  RolltuiMenu* menu_ = rolltui_menu_new();
  ThemeEdit current_;
  UndoStack<ThemeEdit> undo_;
  std::optional<ThemeEdit> preview_;  // the committed value while a live change is shown
  std::vector<PaletteEntry> palette_;
  std::vector<RolltuiFix> fixes_;
  std::vector<std::string> presets_, shipped_;
  bool may_write_shipped_ = false;
  unsigned char mode_ = ROLLTUI_MODE_DARK;
  // The one live effect map (see the struct comment above); never null. Default-built
  // empty (a still UI) so a fresh editor with nothing loaded yet has one to hand over.
  RolltuiEffectMap* dark_effects_ = rolltui_effect_map_new(ROLLTUI_EFFECT_STATE_COUNT, default_effect_fallback_role());
  // The loaded preset's "defs", for palette labels — OWNED (a clone: `load`'s `colours`
  // argument is a borrow that may be freed or replaced by the caller before the next
  // load), freed on the next load and in the destructor. May be null (no defs).
  RolltuiJsonValue* defs_ = nullptr;
  std::string last_level_;  // the level id after the previous event (to notice an ascend)
  std::string status_;

  static unsigned char default_effect_fallback_role();
};

}  // namespace rolltui::tools
