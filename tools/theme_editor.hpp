#pragma once
//
// rolltui/tools/theme_editor.hpp — THE C++ SHAPE OVER THE LIBRARY'S THEME EDITOR.
//
// The editor itself is `rolltui/c/rolltui_theme_editor.h`, and everything it does and refuses
// to do is stated there. This file is what a C++ host writes against: one handle, the same
// method names the studio already calls, and two value types a C++ caller wants — a whole
// theme edit it can hold and hand back, and one palette entry.
//
// IT IS A SHAPE, NEVER A SECOND IMPLEMENTATION. Every method below forwards; nothing here
// decides anything. The editor moved into the library because a widget kind is a model plus a
// draw and a handle, and an app that names `theme` in its layout gets all three with no code —
// which is exactly what one program owning this model made impossible.
//
#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_menu.h"
#include "rolltui/c/rolltui_theme.h"
#include "rolltui/c/rolltui_theme_editor.h"
#include "tool_str.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tool_actions.hpp"

namespace rolltui::tools {

// The C types, under the names every editor already writes them as.
using MenuItem = RolltuiMenuItem;
using InputSpec = RolltuiInputSpec;
using Style = RolltuiStyle;
using Color = RolltuiStyleColor;

// A WHOLE THEME EDIT as a C++ value: both variants' styles, names and "meta". The model's own
// copy is opaque (an owning pointer in a shared struct is a deep copy in C++ and a shallow one
// in C, which is a double free waiting for its first C consumer); this is a host-side value
// built from the model's accessors and handed back through `replace`.
struct ThemeEdit {
  std::array<RolltuiStyle, ROLLTUI_ROLE_COUNT> dark{}, light{};
  std::string dark_name, light_name;
  RolltuiJsonValue* dark_meta = nullptr;   // OWNED; nullptr when the variant claims none
  RolltuiJsonValue* light_meta = nullptr;  // OWNED; ditto

  ThemeEdit() = default;
  // ADOPTS dark_meta/light_meta — pass a clone when the caller's own copy must survive the
  // call (rolltui_json_clone), or a freshly-owned one.
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
      ThemeEdit copy(o);
      *this = std::move(copy);
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
  std::string id;     // the colour's own spelling
  std::string label;  // the id, prefixed by the `defs` name for that colour where there is one
};

class ThemeEditor {
 public:
  struct Outcome {
    enum class Kind { None, Changed, Committed, SaveAs, WriteShipped, LoadPreset, ResetLoaded, ResetBuiltin, Check, Closed };
    Kind kind = Kind::None;
    std::string value;  // SaveAs: the name; WriteShipped: the shipped name; LoadPreset: the preset name
    bool operator==(const Outcome&) const = default;
  };

  // `ctx` is the session this editor edits within. The model needs none; the parameter stays
  // because a host holds one and the studio's other three editors take it.
  explicit ThemeEditor(RolltuiContext* ctx) : ctx_(ctx), e_(rolltui_theme_editor_new()) {}
  ~ThemeEditor() { rolltui_theme_editor_free(e_); }
  ThemeEditor(const ThemeEditor&) = delete;
  ThemeEditor& operator=(const ThemeEditor&) = delete;

  bool load(const RolltuiJsonValue* colours, RolltuiThemeReport* report) {
    return rolltui_theme_editor_load(e_, colours, report) != 0;
  }
  void set_presets(const std::vector<std::string>& names);
  void set_shipped(const std::vector<std::string>& names, bool may_write);
  void set_mode(unsigned char m) { rolltui_theme_editor_set_mode(e_, m); }
  unsigned char mode() const { return rolltui_theme_editor_mode(e_); }

  // The theme being previewed right now (committed + any live, uncommitted change) — a BORROW
  // of the model's own storage, valid until the next edit.
  const RolltuiStyle* current() const { return rolltui_theme_editor_styles(e_, mode(), 0); }
  ThemeEdit committed() const;
  bool previewing() const { return rolltui_theme_editor_previewing(e_) != 0; }
  // The committed variants as one file object. OWNED — free it with `rolltui_json_free`, or
  // hand it straight to a preset store, which adopts it.
  RolltuiJsonValue* colours_json(std::string_view name) const {
    return rolltui_theme_editor_colours_json(e_, name.data(), name.size());
  }

  RolltuiMenu* menu() { return rolltui_theme_editor_menu(e_); }
  const RolltuiMenu* menu() const { return rolltui_theme_editor_menu(e_); }

  // Events already routed to the editor's window. `nav` is the host's bindings (the menu, edit
  // and editor scopes are read).
  Outcome handle(const RolltuiEvent* e, const RolltuiBindings* nav);
  Outcome handle(const RolltuiEvent* e) { return handle(e, editor_bindings(ctx_)); }
  bool undo() { return rolltui_theme_editor_undo(e_) != 0; }
  bool redo() { return rolltui_theme_editor_redo(e_) != 0; }
  std::size_t undo_depth() const { return rolltui_theme_editor_undo_depth(e_); }
  std::size_t redo_depth() const { return rolltui_theme_editor_redo_depth(e_); }
  // Puts a whole theme edit in as the new committed value (a reset, a generated theme).
  // `dark_effects` is ADOPTED; nullptr keeps whatever effects the editor already has.
  void replace(ThemeEdit v, RolltuiEffectMap* dark_effects = nullptr);

  // For a host's sample box: the role the menu is on (nullopt at the top levels), the colour
  // highlighted in a palette choice, and the three lines the editor describes itself with.
  std::optional<unsigned char> focused_role() const {
    unsigned char r = 0;
    return rolltui_theme_editor_focused_role(e_, &r) ? std::optional<unsigned char>(r) : std::nullopt;
  }
  std::optional<Color> highlighted_color() const {
    Color c{};
    return rolltui_theme_editor_highlighted_color(e_, &c) ? std::optional<Color>(c) : std::nullopt;
  }
  // REFILLED into a string the caller keeps: the studio draws this every frame an editor is
  // open. The returning form is one copy over it, for a test that reads it.
  void status_line(std::string& out) const;
  std::string status_line() const;
  std::string badges_line() const;   // "badges: dark readable cvd-safe" (computed, never declared)
  std::string report() const;        // the full contrast / colour-vision report

  // The palette offered to every colour choice, and the auto-fix proposals for the variant
  // being edited. Both are the model's own storage, read one entry at a time rather than
  // rebuilt into a container per call.
  std::size_t palette_count() const { return rolltui_theme_editor_palette_count(e_); }
  PaletteEntry palette_at(std::size_t i) const;
  std::size_t fix_count() const { return rolltui_theme_editor_fix_count(e_); }
  const RolltuiFix* fix_at(std::size_t i) const { return rolltui_theme_editor_fix_at(e_, i); }

 private:
  RolltuiContext* ctx_;  // BORROWED: the session this editor edits within
  RolltuiThemeEditor* e_;
};

}  // namespace rolltui::tools
