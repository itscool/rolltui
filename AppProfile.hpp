#pragma once
//
// rolltui/AppProfile.hpp — WHAT A LAYOUT MAY NAME INSIDE ONE APP (plan/phase-11.md,
// milestone 4). A profile is a file an application PUBLISHES about itself, so a tool
// that authors screens for it can preview them truthfully without being it.
//
// The problem it solves, measured 2026-09-02 rather than assumed: the studio could only
// edit the screen it was itself rendering. Opening roll's own layout in it drew an error
// panel where roll draws its approval modal, resolved `menu:main` to the STUDIO's shipped
// menu rather than roll's, and filled `rows:status` with the studio's facts (theme,
// layout, size) where roll shows local/cloud/turns/tokens/spend. So the preview was wrong
// in exactly the places the target app differs — which are the places you are designing
// for.
//
// WHAT IS IN IT, and why each part is a thing only the app can know:
//   - sources: the DOCUMENTS, ROW SOURCES, SUBMIT TARGETS and NOTES a host binds, each
//     with SAMPLE CONTENT. The names make the design editor's Source field a CHOICE
//     instead of free text; the samples are what makes the preview look like the app.
//   - kinds: the widget kinds the app REGISTERS (Phase 11 m3 — `approval`, `details`).
//     A tool cannot build another program's widget, so it draws a labelled placeholder;
//     what it must not do is draw an error panel for a window that is perfectly correct.
//   - menus: the app's own menu FILES, verbatim. Phase 10 m3's middle rung is "the host's
//     own embedded menus", and that rung differs between binaries BY DESIGN — so the only
//     way to resolve `menu:main` as roll is to be handed roll's.
//   - actions: what the app's screens declare, so a menu item naming one shows its key
//     rather than being reported as undeclared.
//   - min sizes: the app's defaults, which a NEW layout starts from (milestone 5) — the
//     one place inheriting is right, and it is right because it is inherited from the
//     TARGET rather than from whatever screen happened to be open.
//
// WHAT IS NOT IN IT: colours, chords, and the layouts themselves. Those are the shared
// preset directory's, which both binaries already read — a profile describes the app's
// VOCABULARY, never its taste. Keeping that line is what stops the profile becoming a
// second way to ship a theme.
//
// A profile is DATA AN APP PUBLISHES ABOUT ITSELF, so it is generated (`roll profile`),
// never hand-maintained: a hand-written one drifts from the binary silently, which is the
// failure this file exists to remove one level down.
//
#include <optional>
#include <string>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Json.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Widgets.hpp"

namespace rolltui {

struct AppProfile {
  std::string app;                 // "roll"
  int min_width = 0;               // the app's default thresholds (0 = it states none)
  int min_height = 0;
  std::vector<ActionDecl> actions;  // what its screens declare

  // A kind the app REGISTERS (m3). A tool that is not this app cannot build the widget,
  // so it previews a labelled placeholder — never an error panel.
  struct Kind {
    std::string name;
    SourceRule rule = SourceRule::Forbidden;
    std::string describes;  // what its source names, for the editor's hint
    bool operator==(const Kind&) const = default;
  };
  std::vector<Kind> kinds;

  struct Document {
    std::string name;
    std::string sample;  // markdown, previewed verbatim
    bool operator==(const Document&) const = default;
  };
  struct RowSource {
    std::string name;
    std::vector<std::pair<std::string, std::string>> sample;  // label / value
    bool operator==(const RowSource&) const = default;
  };
  struct MenuFile {
    std::string name;
    std::string json;  // the file's text, verbatim
    bool operator==(const MenuFile&) const = default;
  };
  std::vector<Document> documents;
  std::vector<RowSource> rows;
  std::vector<std::string> submits;  // input targets
  std::vector<std::string> notes;    // input note sources
  std::vector<MenuFile> menus;

  bool operator==(const AppProfile&) const = default;
};

struct AppProfileReport {
  std::string error;                      // unusable
  std::vector<std::string> unknown_keys;
  std::vector<std::string> bad_values;
  bool clean() const { return error.empty() && unknown_keys.empty() && bad_values.empty(); }
  std::string summary() const;
};

std::optional<AppProfile> load_app_profile(std::string_view json_text, AppProfileReport& report);
std::optional<AppProfile> load_app_profile(const json::Value& v, AppProfileReport& report);
json::Value app_profile_to_json(const AppProfile& p);

// Every CONTENT a layout may name inside this app, in the design editor's order: the
// library's own kinds paired with the profile's source names, then the app's registered
// kinds. This is what turns milestone 5's free-text Source box into a CHOICE — and it is
// a choice because the profile KNOWS, not because someone typed a list.
std::vector<std::string> profile_contents(const AppProfile& p);

// Mount the profile into a `Windows`: register its kinds as labelled placeholders, bind
// its samples under their names, and add its menu files as the HOST rung. One call, so a
// tool's `--app` wiring is one line and cannot half-apply a profile.
//
// The placeholders are drawn by the library rather than by the tool for the same reason
// the error panel is: what a window that cannot be built looks like is a property of the
// library, not of whoever happens to be previewing.
void mount_app_profile(Windows& windows, const AppProfile& p);

}  // namespace rolltui
