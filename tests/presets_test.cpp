//
// presets_test.cpp — the preset system (milestone 11 / 11e, Phase 10 m1) on all three
// domains: the five rules in Presets.hpp, the shipped files against the built-in themes
// and layouts, the four-rung precedence table (every combination of present/absent
// rungs), the file format's report, and the OSC 11 reply parser with the light/dark
// rule. Phase 10 m1 adds the Layout domain and the once-only migration that carries a
// Phase 9 theme.working.json's layout part across — the section named THE MIGRATION
// below, whose control (a build that does not copy the part) must fail before the code
// exists, because losing it would be silent.
// Runs in a scratch directory under $TMPDIR it creates and removes.
//
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rolltui/Presets.hpp"
#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;
namespace fs = std::filesystem;

#ifndef ROLLTUI_PRESETS_DIR
#error "ROLLTUI_PRESETS_DIR must point at rolltui/presets/themes"
#endif
#ifndef ROLLTUI_LAYOUTS_DIR
#error "ROLLTUI_LAYOUTS_DIR must point at rolltui/presets/layouts"
#endif

namespace {

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return s;
}
void write_file(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  out << s;
}

}  // namespace

int main() {
  const char* t = std::getenv("TMPDIR");
  const fs::path world = fs::path(t && *t ? t : "/tmp") / ("rolltui_presets_test_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(world, ec);
  fs::create_directories(world);
  const std::string dir = (world / "rolltui").string();

  // ---- the shipped presets (rule 5) ----
  {
    std::vector<std::string_view> names = ThemePresets::shipped_names();
    check(!names.empty() && names[0] == "default", "a shipped 'default' always exists and is listed first");
    check(ThemePresets::is_shipped("default") && ThemePresets::is_shipped("mono") && !ThemePresets::is_shipped("mine"), "is_shipped by name");
    // The file that ships is what the embedded table holds, byte for byte.
    for (std::string_view n : names) {
      const std::string on_disk = read_file(fs::path(ROLLTUI_PRESETS_DIR) / (std::string(n) + ".json"));
      check(!on_disk.empty() && on_disk == ThemePresets::shipped_json(n), "shipped '" + std::string(n) + "' embeds the file in rolltui/presets/themes verbatim");
    }
    // The shipped colours equal Theme.cpp's built-ins — two definition sites, kept
    // equal by this check (the theme grep control covers only .cpp/.hpp).
    const ThemePreset* d = ThemePresets::shipped("default");
    ThemeLoadReport rep;
    std::optional<Theme> dark = resolve_colours(*d, ThemeMode::Dark, rep);
    check(dark && rep.clean() && dark->styles == builtin_theme("default-dark")->styles, "shipped 'default' at dark is the built-in default-dark, role for role");
    std::optional<Theme> light = resolve_colours(*d, ThemeMode::Light, rep);
    check(light && rep.clean() && light->styles == builtin_theme("default-light")->styles, "…and at light the built-in default-light");
    const ThemePreset* m = ThemePresets::shipped("mono");
    std::optional<Theme> mono = resolve_colours(*m, ThemeMode::Dark, rep);
    check(mono && rep.clean() && mono->styles == builtin_theme("mono")->styles, "shipped 'mono' is the built-in mono");
    // Phase 12 m6: the shipped files carry the built-ins' MOTION too. Without this the
    // two definition sites could drift in exactly the way that matters least visibly and
    // most: a built-in that spins and a shipped file — the one every session actually
    // runs — that is silently still.
    check(dark->effects == builtin_theme("default-dark")->effects && light->effects == builtin_theme("default-light")->effects,
          "shipped 'default' carries the built-in's effects at both modes");
    check(mono->effects == builtin_theme("mono")->effects, "…and shipped 'mono' the mono theme's own");
    check(!dark->effects.empty() && !mono->effects.empty() && !(dark->effects == mono->effects),
          "…and the two are genuinely different looks, not one map copied twice");
    check(d->mode == "auto" && d->depth == "auto", "shipped 'default' is mode auto, depth auto");
    check(ThemePresets::shipped("default-dark")->mode == "dark" && ThemePresets::shipped("default-light")->mode == "light",
          "'default-dark' / 'default-light' are the same colours pinned to a mode");
    // theme_pair_to_json_value round-trips both variants exactly (the shipped file was
    // produced by it).
    json::Value pair = theme_pair_to_json_value(*builtin_theme("default-dark"), *builtin_theme("default-light"), "x");
    std::optional<Theme> pd = load_theme(pair, ThemeMode::Dark, rep), pl = load_theme(pair, ThemeMode::Light, rep);
    check(pd && pl && pd->styles == builtin_theme("default-dark")->styles && pl->styles == builtin_theme("default-light")->styles,
          "theme_pair_to_json_value loads back to each variant exactly");
  }

  // ---- start: no working copy → default (rules 2, 5) ----
  ThemePresets store({dir, false, ""});
  {
    PresetLoadReport rep = store.start();
    check(rep.clean() && !rep.notes.empty() && rep.notes[0].find("no working copy") == 0, "a fresh directory starts from 'default' and says so [" + (rep.notes.empty() ? "" : rep.notes[0]) + "]");
    check(store.origin() == "default" && store.label() == "default" && !store.modified(), "label 'default', unmodified");
    check(!fs::exists(store.working_path()), "starting writes nothing");
    check(store.working() == *ThemePresets::shipped("default"), "the working copy IS the shipped default (rule 1: the whole domain)");
  }
  // ---- an edit autosaves and changes the label by comparison (rules 2, 4) ----
  {
    const std::uint64_t v0 = store.version();
    store.set_depth("16");
    check(store.version() > v0 && store.label() == "default (modified)" && store.modified(), "a depth change bumps the version and the label reads 'default (modified)'");
    check(fs::exists(store.working_path()), "…and autosaved " + store.working_path());
    std::string err;
    json::Value v = json::parse(read_file(store.working_path()), err);
    check(err.empty() && v.get("preset").as_string() == "default" && v.get("depth").as_string() == "16" && v.has("colours"),
          "the working file records its origin preset and the whole domain");
    check(!v.has("layout"), "…and NOT a layout: the Theme domain is colours + mode + depth (Phase 10 m1)");
    check(fs::directory_iterator(dir) != fs::directory_iterator() && !fs::exists(store.working_path() + ".tmp." + std::to_string(::getpid())), "no temp file is left behind (written by rename)");
    store.set_depth("auto");
    check(store.label() == "default" && !store.modified(), "putting the depth back makes it 'default' again — identity is by comparison, not a dirty flag");
    store.set_mode("light");
    check(store.label() == "default (modified)", "a mode change is a modification");
  }
  // ---- restart picks the working copy up (rule 2: nothing is lost) ----
  {
    ThemePresets again({dir, false, ""});
    PresetLoadReport rep = again.start();
    check(rep.clean() && again.working() == store.working() && again.label() == "default (modified)",
          "a second store on the same directory loads the autosaved working copy with its label [" + rep.summary() + "]");
  }
  // ---- save as (rule 3) ----
  {
    std::string err;
    check(store.save_as("default", false, err) == SaveResult::RefusedShipped, "save-as over a shipped name is refused by name (rule 5): " + err);
    check(store.save_as("../evil", false, err) == SaveResult::BadName && store.save_as(".hidden", false, err) == SaveResult::BadName, "a path-like or dot name is refused");
    check(store.save_as("mine", false, err) == SaveResult::Saved && err.empty(), "save-as 'mine' saves");
    check(store.label() == "mine" && store.origin() == "mine" && !store.modified(), "…and the working copy is now 'mine', unmodified");
    check(fs::exists(store.preset_path("mine")), "the preset file exists at " + store.preset_path("mine"));
    std::vector<PresetInfo> list = store.list();
    bool has_mine = false, shipped_first = !list.empty() && list[0].shipped && list[0].name == "default";
    for (const PresetInfo& p : list) if (p.name == "mine" && !p.shipped && p.path == store.preset_path("mine")) has_mine = true;
    check(shipped_first && has_mine, "list() has the shipped presets first and the user preset with its path");
    store.set_depth("256");
    check(store.label() == "mine (modified)", "an edit after saving reads 'mine (modified)'");
    check(store.save_as("mine", false, err) == SaveResult::ExistsAsk, "save-as over an existing user preset asks once: " + err);
    check(store.save_as("mine", true, err) == SaveResult::Saved && store.label() == "mine", "…and saves with confirmation");
    // Load copies (rule 3): the preset is read-only — editing the working copy does
    // not touch the file.
    store.set_depth("16");
    PresetLoadReport rep;
    check(store.load("mine", rep) && rep.clean() && store.working().depth == "256" && store.label() == "mine", "load copies the preset back into the working copy");
    check(store.load("default", rep) && store.label() == "default" && store.working() == *ThemePresets::shipped("default"), "load 'default' restores the shipped preset whole");
    check(!store.load("nope", rep) && rep.error.find("no theme preset 'nope'") == 0, "loading an unknown name fails with a named error [" + rep.error + "]");
    check(store.label() == "default", "…and leaves the working copy alone");
  }
  // ---- persist=false: a flag fills the run's copy without writing it ----
  {
    ThemePresets s2({(world / "flags").string(), false, ""});
    s2.start();
    PresetLoadReport rep;
    check(s2.load("mono", rep, /*persist=*/false) && s2.label() == "mono" && !fs::exists(s2.working_path()),
          "load with persist=false makes the working copy 'mono' in memory and writes nothing");
    s2.set_mode("dark");
    check(fs::exists(s2.working_path()) && s2.label() == "mono (modified)", "the first real edit writes what was running (mono + the edit)");
    ThemePresets s3({(world / "flags").string(), false, ""});
    s3.start();
    check(s3.origin() == "mono" && s3.working().mode == "dark", "…and a restart comes back to it");
  }
  // ---- the editor's privilege (rule 5) ----
  {
    const std::string shipped_dir = (world / "shipped").string();
    ThemePresets editor({(world / "editor").string(), true, shipped_dir});
    editor.start();
    editor.set_mode("light");
    std::string err;
    check(editor.save_as("default", false, err) == SaveResult::Saved && fs::exists(fs::path(shipped_dir) / "default.json"),
          "with may_write_shipped the editor writes 'default' into the shipped directory: " + err);
    std::string e2;
    json::Value v = json::parse(read_file(fs::path(shipped_dir) / "default.json"), e2);
    check(e2.empty() && v.get("mode").as_string() == "light" && v.get("name").as_string() == "default", "…as a complete preset file");
    check(editor.label() == "default", "…and the working copy is 'default' again (the file it just wrote)");
  }
  // ---- a broken working copy is reported, not served ----
  {
    const std::string bad = (world / "bad").string();
    write_file(fs::path(bad) / "theme.working.json", "{ not json");
    ThemePresets s({bad, false, ""});
    PresetLoadReport rep = s.start();
    check(!rep.error.empty() && rep.error.find("unreadable") != std::string::npos && s.label() == "default", "an unparseable working copy: error named, default served");
    write_file(fs::path(bad) / "theme.working.json", R"({"mode":"sideways","depth":"256","colours":{"roles":{"text":{"fg":"none"}}},"extra":1})");
    ThemePresets s4({bad, false, ""});
    rep = s4.start();
    check(rep.error.empty() && rep.bad_values.size() == 1 && rep.bad_values[0].find("mode") == 0 && rep.unknown_keys.size() == 1 && rep.unknown_keys[0] == "extra" &&
              rep.colours.missing_roles.size() == kRoleCount - 1,
          "a loadable working copy with problems loads and reports each: bad mode, unknown key, missing roles [" + rep.summary() + "]");
    check(s4.working().mode == "auto" && s4.working().depth == "256", "the bad value keeps its default; the good parts load");
    // THE SENTENCE ITSELF, not just its inputs (Phase 17 m2a, +1 assertion). Every use of
    // summary() in this suite was inside a check NAME — so the composition it performs (which
    // parts, in what order, "bad: "/"unknown: "/"colours: " prefixes, "; " between them) was
    // asserted by nothing at all, and moving it to C could have changed every word silently.
    {
      const std::string one = rep.summary();
      check(one.find("bad: mode") == 0 && one.find("; unknown: extra") != std::string::npos &&
                one.find("; colours: " + std::to_string(kRoleCount - 1) + " roles missing (inherit text)") !=
                    std::string::npos,
            "…and summary() composes them in order with their prefixes [" + one + "]");
    }
  }
  // ---- colours-only theme files ----
  {
    const std::string d2 = (world / "files").string();
    ThemePresets s({d2, false, ""});
    s.start();
    s.set_depth("mono");
    write_file(fs::path(d2) / "colours.json", theme_to_json(*builtin_theme("mono")));
    PresetLoadReport rep;
    check(s.load((fs::path(d2) / "colours.json").string(), rep) && !rep.notes.empty() && rep.notes.back().find("colours-only") != std::string::npos,
          "a colours-only theme file loads into the colours part with a note");
    ThemeLoadReport tr;
    check(resolve_colours(s.working(), ThemeMode::Dark, tr)->styles == builtin_theme("mono")->styles && s.working().depth == "mono" && s.label() == "default (modified)",
          "…the look is mono, mode and depth kept, and the label says modified against 'default'");
    // A user preset saved under a shipped name's file is never listed as a user preset.
    write_file(fs::path(d2) / "themes" / "default.json", "{}");
    bool dup = false;
    for (const PresetInfo& p : s.list()) if (p.name == "default" && !p.shipped) dup = true;
    check(!dup, "a user file named like a shipped preset does not shadow or duplicate it");
  }
  // ---- the file format, round trip ----
  {
    const ThemePreset* d = ThemePresets::shipped("default");
    PresetLoadReport rep;
    std::optional<ThemePreset> back = theme_preset_from_json(theme_preset_to_json(*d, "default"), rep);
    check(back && rep.clean() && *back == *d, "theme_preset_to_json / theme_preset_from_json round-trips the whole domain");
    json::Value no_colours = json::Value::object();
    check(!theme_preset_from_json(no_colours, rep) && rep.error.find("colours") != std::string::npos, "a preset without colours is unusable");
    check(!theme_preset_to_json(*d, "x").has("layout"), "a written theme preset carries no layout (Phase 10 m1)");
    // A Phase 9 preset file: its layout part is IGNORED, by name, and the file is still
    // clean — the part was valid, it just is not the Theme's any more.
    json::Value old = theme_preset_to_json(*d, "x");
    old.set("layout", layout_to_json_value(*builtin_layout("stacked")));
    back = theme_preset_from_json(old, rep);
    check(back && *back == *d && rep.clean() && rep.notes.size() == 1 && rep.notes[0].find("\"layout\": ignored") == 0,
          "a Phase 9 preset file loads its colours and reports the layout part as ignored, by name [" + (rep.notes.empty() ? rep.summary() : rep.notes[0]) + "]");
  }
  // ---- precedence: flag > env > working > builtin, all 8 present/absent combinations ----
  {
    bool all = true;
    for (int mask = 0; mask < 8; ++mask) {
      const std::string_view flag = (mask & 4) ? "F" : "", env = (mask & 2) ? "E" : "", working = (mask & 1) ? "W" : "";
      const Resolved r = resolve_setting(flag, env, working, "B");
      const Resolved want = (mask & 4) ? Resolved{"F", Rung::Flag} : (mask & 2) ? Resolved{"E", Rung::Env} : (mask & 1) ? Resolved{"W", Rung::Working} : Resolved{"B", Rung::Builtin};
      if (!(r == want)) { all = false; check(false, "precedence mask " + std::to_string(mask) + ": got " + r.value + " from " + std::string(rung_name(r.rung))); }
    }
    check(all, "the first non-empty rung wins in every one of the 8 combinations, and the answer names its rung");
    check(rung_name(Rung::Flag) == "flag" && rung_name(Rung::Builtin) == "built-in default", "rung names");
    check(setting("theme") && setting("layout") && setting("theme_mode") && setting("color_depth") && setting("bindings") && !setting("frontend"),
          "one table holds all five settings across the three domains (frontend is a host's)");
    check(setting("theme")->domain == Domain::Theme && setting("layout")->domain == Domain::Layout && setting("bindings")->domain == Domain::Bindings &&
              setting("theme_mode")->domain == Domain::Theme && domain_name(Domain::Layout) == "layout",
          "…each row names its own domain (Phase 10 m1: 'layout' is no longer a Theme row)");
    check(setting("theme")->builtin == "default" && setting("layout")->builtin == "default" && setting("theme_mode")->builtin == "auto" &&
              setting("color_depth")->builtin == "auto",
          "built-in defaults: default / default / auto / auto");
    PresetLoadReport lrep;
    store.load("default", lrep);  // (the store from above)
    store.set_mode("dark");
    check(working_value(store, "theme") == "default" && working_value(store, "theme_mode") == "dark" && working_value(store, "color_depth") == "auto" &&
              working_value(store, "layout").empty(),
          "working_value reads each of the Theme domain's settings, and none of another domain's");
  }
  // ---- the Layout domain (Phase 10 m1): the same five rules on the third domain ----
  {
    const std::string ldir = (world / "layouts").string();
    LayoutPresets ls({ldir, false, ""});
    PresetLoadReport rep = ls.start();
    check(rep.clean() && ls.label() == "default" && ls.working() == *builtin_layout("default"),
          "a fresh directory starts from the shipped default layout (rule 5), label 'default'");
    // The shipped presets ARE the built-ins: one definition site, asserted anyway.
    std::vector<std::string_view> names = LayoutPresets::shipped_names();
    check(names.size() == 4 && names[0] == "default", "four shipped layouts, 'default' first");
    bool all_builtin = true;
    for (std::string_view n : names) {
      const std::string on_disk = read_file(fs::path(ROLLTUI_LAYOUTS_DIR) / (std::string(n) + ".json"));
      if (on_disk.empty() || on_disk != LayoutPresets::shipped_json(n)) { all_builtin = false; check(false, "shipped layout '" + std::string(n) + "' does not embed its file verbatim"); }
      if (!builtin_layout(n) || !(*LayoutPresets::shipped(n) == *builtin_layout(n))) { all_builtin = false; check(false, "shipped layout '" + std::string(n) + "' differs from builtin_layout()"); }
    }
    check(all_builtin, "every shipped layout embeds rolltui/presets/layouts/<name>.json verbatim AND is builtin_layout(name) — one definition site");
    // Rule 2: one working copy, autosaved; rule 4: the label by comparison.
    Layout wide = *builtin_layout("default");
    wide.base.root.children[1].size = SplitSize::fixed(Dim::abs(48));  // a wider status panel
    ls.set_working(wide);
    check(ls.label() == "default (modified)" && fs::exists(ls.working_path()) && ls.working_path() == ldir + "/layout.working.json",
          "an edit autosaves layout.working.json and reads 'default (modified)'");
    LayoutPresets again({ldir, false, ""});
    rep = again.start();
    check(rep.clean() && again.working() == wide && again.label() == "default (modified)",
          "a restart loads the autosaved working copy exactly, with its label [" + rep.summary() + "]");
    // Rules 3 and 5.
    std::string err;
    check(ls.save_as("default", false, err) == SaveResult::RefusedShipped, "save-as over a shipped layout name is refused (rule 5)");
    check(ls.save_as("wide", false, err) == SaveResult::Saved && ls.label() == "wide" && fs::exists(ls.preset_path("wide")), "save-as 'wide' saves and becomes the origin");
    check(ls.load("stacked", rep) && ls.working() == *builtin_layout("stacked") && ls.label() == "stacked", "load copies a shipped layout back whole (rule 1)");
    check(ls.load("wide", rep) && ls.working() == wide, "…and the user preset back");
    // A file dropped into <dir>/layouts is a preset: the Phase 9 discovery, by the
    // domain's own mechanics now.
    write_file(fs::path(ldir) / "layouts" / "two.json", layout_to_json(*builtin_layout("no-panel")));
    bool has_two = false;
    for (const PresetInfo& p : ls.list()) if (p.name == "two" && !p.shipped) has_two = true;
    check(has_two, "a layout file dropped into <dir>/layouts is listed as a preset by name");
    std::optional<Layout> l = ls.get("two", rep);
    check(l && rep.clean() && l->name == "no-panel", "…and loads (its own \"name\" and the file name may differ)");
    check(ls.get((fs::path(ldir) / "layouts" / "two.json").string(), rep).has_value(), "…and resolves by path too");
    check(!ls.get("nothing", rep) && rep.error.find("no layout preset 'nothing'") == 0, "an unknown name is a named error [" + rep.error + "]");
    check(!ls.load("nothing", rep) && ls.label() == "wide", "…and leaves the working copy alone");
    // A layout file's own problems are reported through the layout sub-report.
    write_file(fs::path(ldir) / "layouts" / "odd.json", R"({"name":"odd","colour":"blue","root":{"content":"transcript","border":"triple"}})");
    check(ls.load("odd", rep) && !rep.clean() && rep.layout.unknown_keys.size() == 1 && rep.layout.unknown_keys[0] == "colour" && rep.layout.bad_values.size() == 1 &&
              ls.working().base.root.border == Border::None,
          "a layout with an unknown key and a bad value loads, reports both, and keeps the default [" + rep.summary() + "]");
    check(working_value(ls, "layout") == "odd" && working_value(ls, "theme").empty(), "working_value on the Layout store is its origin, and nothing else's");
  }
  // ---- THE MIGRATION (Phase 10 m1): the layout leaves the Theme domain, once ----
  // The failure guarded here is silent: a Phase 9 install keeps the user's layout inside
  // theme.working.json, and a Theme domain that no longer parses the part would drop it
  // on the very first autosave with nothing said and nothing to see. The control for
  // this section is a build whose migrate_theme_layout() does not copy the part: the
  // three checks named "THE LAYOUT SURVIVED", "…and it is the EDITED one" and "the theme
  // file no longer carries a layout" must fail there.
  {
    const std::string m = (world / "migrate").string();
    // A Phase 9 working copy: the shipped 'mono' colours plus an EDITED layout — a
    // panel-left with a 20-cell status window, which is no shipped layout, so "the
    // layout survived" cannot pass by landing on a default.
    Layout edited = *builtin_layout("panel-left");
    edited.base.root.children[0].size = SplitSize::fixed(Dim::abs(20));
    json::Value phase9 = theme_preset_to_json(*ThemePresets::shipped("mono"), "mono");
    phase9.set("preset", json::Value::string("mono"));
    phase9.set("layout", layout_to_json_value(edited));
    write_file(fs::path(m) / "theme.working.json", json::dump(phase9, 2) + "\n");

    MigrationReport mr = migrate_theme_layout(m);
    check(mr.error.empty() && mr.moved && mr.rewrote_theme && mr.layout_name == "panel-left" && !mr.notes.empty(),
          "a theme working copy carrying a layout: the part is moved out and the theme file rewritten, and it says so [" +
              (mr.notes.empty() ? mr.error : mr.notes[0]) + "]");
    LayoutPresets ls({m, false, ""});
    PresetLoadReport lrep = ls.start();
    check(lrep.clean() && ls.origin() == "panel-left" && ls.modified(), "THE LAYOUT SURVIVED: a layout working copy exists, labelled 'panel-left (modified)' [" + lrep.summary() + "]");
    check(ls.working() == edited, "…and it is the EDITED one, window for window, not a shipped layout that merely looks plausible");
    ThemePresets ts({m, false, ""});
    PresetLoadReport trep = ts.start();
    check(trep.clean() && ts.origin() == "mono" && ts.working() == *ThemePresets::shipped("mono"),
          "…and the theme working copy is otherwise untouched: still 'mono', unmodified [" + trep.summary() + "]");
    std::string perr;
    check(!json::parse(read_file(fs::path(m) / "theme.working.json"), perr).has("layout") && perr.empty(),
          "the theme file no longer carries a layout — so nothing reports it ignored, run after run");
    MigrationReport twice = migrate_theme_layout(m);
    check(!twice.moved && !twice.rewrote_theme && twice.notes.empty() && twice.error.empty(), "running it again has nothing to do");
    // The other direction: a layout working copy ALREADY exists. The stale part must
    // never overwrite it — this is the once-only half of the rule.
    const std::string m2 = (world / "migrate2").string();
    write_file(fs::path(m2) / "theme.working.json", json::dump(phase9, 2) + "\n");
    LayoutPresets mine({m2, false, ""});
    mine.load("stacked", lrep);  // the user has since chosen their own
    MigrationReport mr2 = migrate_theme_layout(m2);
    check(!mr2.moved && mr2.rewrote_theme && mr2.notes.size() == 1 && mr2.notes[0].find("already exists") != std::string::npos,
          "an existing layout working copy is NOT overwritten; the stale part is dropped and the reason said [" + (mr2.notes.empty() ? "" : mr2.notes[0]) + "]");
    LayoutPresets mine2({m2, false, ""});
    mine2.start();
    check(mine2.working() == *builtin_layout("stacked") && mine2.label() == "stacked", "…and the user's own layout is still theirs");
    // A fresh Phase 10 install, and a theme working copy whose layout part is junk.
    check(!migrate_theme_layout((world / "nothing-here").string()).moved, "a fresh install has nothing to migrate and says nothing");
    const std::string m3 = (world / "migrate3").string();
    json::Value junk = phase9;
    junk.set("layout", json::Value::string("not a layout"));
    write_file(fs::path(m3) / "theme.working.json", json::dump(junk, 2) + "\n");
    MigrationReport mr3 = migrate_theme_layout(m3);
    check(!mr3.error.empty() && !mr3.moved && !mr3.rewrote_theme && fs::exists(fs::path(m3) / "theme.working.json") && !fs::exists(fs::path(m3) / "layout.working.json"),
          "an unusable layout part is an error and NOTHING is changed — the user still has the bytes [" + mr3.error + "]");
  }
  // ---- the Bindings domain (milestone 17): the same five rules on the second domain ----
  {
    const std::string bdir = (world / "bindings").string();
    BindingsPresets bs({bdir, false, ""});
    PresetLoadReport rep = bs.start();
    check(rep.clean() && bs.label() == "default" && bs.working() == default_bindings(), "a fresh directory starts from the shipped default bindings (rule 5), label 'default'");
    check(BindingsPresets::is_shipped("default") && BindingsPresets::shipped_json("default") == default_bindings_json(), "the shipped 'default' is the embedded file, verbatim");
    Bindings vim = bs.working();
    vim.bind("input.word_left", *parse_chord("alt+b"));
    vim.bind("input.word_right", *parse_chord("alt+f"));
    bs.set_working(vim);
    check(bs.label() == "default (modified)" && fs::exists(bs.working_path()) && bs.working().action_for(*parse_chord("alt+b"), "input") == "input.word_left",
          "an edit autosaves bindings.working.json and the label reads 'default (modified)' (rules 2, 4)");
    BindingsPresets again({bdir, false, ""});
    rep = again.start();
    check(rep.clean() && again.working() == bs.working() && again.label() == "default (modified)", "a restart loads the autosaved working copy with its label [" + rep.summary() + "]");
    std::string err;
    check(bs.save_as("default", false, err) == SaveResult::RefusedShipped, "save-as over the shipped name is refused (rule 5)");
    check(bs.save_as("vim-ish", false, err) == SaveResult::Saved && bs.label() == "vim-ish" && fs::exists(bs.preset_path("vim-ish")), "save-as 'vim-ish' saves (rule 3) and becomes the origin");
    check(bs.load("default", rep) && bs.working() == default_bindings() && bs.label() == "default", "load copies the shipped default back (rule 1: the whole domain)");
    check(bs.load("vim-ish", rep) && bs.working().action_for(*parse_chord("alt+f"), "input") == "input.word_right", "…and the user preset back");
    // A file that moves Enter is refused by name in the load report; the rest loads.
    write_file(fs::path(bdir) / "bindings" / "bad.json", R"({"name":"bad","bindings":{"input.submit":["ctrl+j"],"input.newline":["enter"],"input.left":["hyper+x"]}})");
    check(bs.load("bad", rep) && !rep.clean() && rep.bindings.bad_values.size() == 2 && rep.bindings.bad_values[0].find("input.newline: 'enter' is always input.submit") == 0 &&
              rep.bindings.bad_chords.size() == 1 && bs.working().action_for(*parse_chord("enter"), "input") == "input.submit",
          "a file binding Enter elsewhere loads with Enter refused by name and restored on submit [" + rep.summary() + "]");
    check(!bs.load("nothing", rep) && rep.error.find("no bindings preset 'nothing'") == 0, "an unknown bindings preset is a named error");
    check(working_value(bs, "bindings") == "bad" && setting("bindings")->domain == Domain::Bindings && setting("bindings")->builtin == "default",
          "the Bindings domain's one setting: 'bindings', built-in 'default'");
    // All three domains in one directory, three working files, none touching another.
    ThemePresets ts({bdir, false, ""});
    ts.start();
    ts.set_mode("light");
    LayoutPresets ls({bdir, false, ""});
    ls.start();
    ls.load("no-panel", rep);
    check(fs::exists(fs::path(bdir) / "theme.working.json") && fs::exists(fs::path(bdir) / "layout.working.json") && fs::exists(fs::path(bdir) / "bindings.working.json") &&
              bs.label() == "bad" && ts.label() == "default (modified)" && ls.label() == "no-panel",
          "a session is Theme X + Layout Y + Bindings Z: three working copies side by side, each with its own label");
  }

  // ---- OSC 11 ----
  {
    std::optional<Color> c = parse_osc11_reply("\x1b]11;rgb:1414/1616/1a1a\x1b\\");
    check(c && *c == Color::rgb(0x14, 0x16, 0x1a), "a 16-bit-per-channel reply (ST-terminated) parses to its top bytes");
    c = parse_osc11_reply("\x1b]11;rgb:ffff/ffff/ffff\a");
    check(c && *c == Color::rgb(255, 255, 255), "a BEL-terminated reply parses");
    c = parse_osc11_reply("junk\x1b]11;rgb:f/8/0\x1b\\more");
    check(c && *c == Color::rgb(255, 136, 0), "1-digit channels scale (f → 255, 8 → 136); surrounding bytes are ignored");
    check(!parse_osc11_reply("\x1b]11;?\x1b\\") && !parse_osc11_reply("\x1b]11;rgb:zz/00/00\x1b\\") && !parse_osc11_reply("hello"),
          "the query itself, a bad digit and no reply at all are nullopt");
    check(mode_for_background(Color::rgb(0x14, 0x16, 0x1a)) == ThemeMode::Dark && mode_for_background(Color::rgb(0xfa, 0xfa, 0xf8)) == ThemeMode::Light,
          "a near-black background is dark, a near-white one light");
    check(mode_for_background(Color::rgb(0x80, 0x80, 0x80)) == ThemeMode::Dark && mode_for_background(Color::rgb(0xc0, 0xc0, 0xc0)) == ThemeMode::Light,
          "mid grey (#808080, luminance 0.22) is dark; #c0c0c0 (0.53) is light — the threshold is relative luminance 0.5");
    check(mode_for_background(Color::none()) == ThemeMode::Dark, "no colour answer: dark");
    check(*mode_from_setting("light") == ThemeMode::Light && !mode_from_setting("auto") && *depth_from_setting("256") == ColorDepth::Ansi256 && !depth_from_setting("auto"),
          "mode/depth setting parsers; auto is nullopt (the host's to settle)");
  }

  fs::remove_all(world, ec);
  // ---- THE C-SIDE DOMAIN DESCRIPTORS, which shipped with no checked-in test ---------------
  // Phase 17 gave Theme/Layout/Bindings C descriptors so a pure-C caller can build a store —
  // the gap TWO separate agents hit independently. They were verified during the port by a
  // scratch program that was never checked in, which means ~200 lines of new C entered the
  // library covered by nothing. **Untested code in a library whose whole argument is its
  // controls is the one thing this session should not ship**, so this is that coverage.
  //
  // It asserts the property the descriptors exist FOR: a C descriptor and the C++ template
  // path must agree about the same shipped presets. If they ever disagree, a host that built
  // its store the C way and one that built it the C++ way would see different defaults —
  // exactly the two-spellings failure CLAUDE.md's vocabulary rule names.
  {
    check(c_theme_domain().parse && c_theme_domain().to_json && c_theme_domain().clone &&
              c_theme_domain().destroy && c_theme_domain().equal && c_theme_domain().shipped_at,
          "the C theme domain fills every slot the store calls through");
    check(c_layout_domain().parse && c_layout_domain().clone && c_layout_domain().destroy &&
              c_layout_domain().equal,
          "…and so does the C layout domain");
    check(c_bindings_domain().parse && c_bindings_domain().clone && c_bindings_domain().destroy &&
              c_bindings_domain().equal,
          "…and the C bindings domain");
    // A CLONE MUST SURVIVE ITS ORIGINAL, which is the exact bug ASan caught during the port:
    // theme_domain_clone allocated without zeroing, and rolltui_str_set then read the
    // uninitialised RolltuiStr as if it were valid. A clone that is merely allocated is not a
    // clone, so this parses one, clones it, destroys the original and reads the copy.
    const RolltuiPresetDomain& d = c_theme_domain();
    const char* name = nullptr;
    const char* text = nullptr;
    std::size_t nlen = 0, tlen = 0;
    check(d.shipped_count() > 0, "the C theme domain reports its shipped presets");
    d.shipped_at(0, &name, &nlen, &text, &tlen);
    check(text != nullptr && tlen > 0, "…and hands one back as TEXT, never a tree");
    if (text && tlen) {
      void* a = d.parse(text, tlen, nullptr);
      check(a != nullptr, "…which the domain parses");
      if (a) {
        void* b = d.clone(a);
        check(b != nullptr && d.equal(a, b), "…a clone equals its original");
        d.destroy(a);
        check(b != nullptr && d.equal(b, b), "…and OUTLIVES it: the clone is whole after the original is destroyed");
        if (b) d.destroy(b);
      }
    }

    // A NULL REPORT ON THE *FAILING* PATH — the case that actually writes into the report and
    // then has nowhere to put it. Both halves of the fix are held here, and the second half
    // needed an instrument this suite did not have. The obvious claim — "ASan's leak checker
    // catches a missing release" — was written, CHECKED, and was false: macOS ships ASan with
    // the leak detector off, and `presets_test` never called `rolltui_shutdown`, so deleting
    // the release leaked through a clean 31/31 sanitizer run (control run 2026-09-05, exit 0,
    // no report). The library's own counter is the instrument that actually exists, and this
    // is the first test outside `budget`/`lifetime` to point it at a single call:
    //   crash-free  holds the guard   (without it this aborts — control run, exit 134)
    //   byte-exact  holds the release (without it live_bytes grows — nothing else would say)
    static const char kBad[] = "{ this is not a theme";
    std::size_t live_before = 0, live_after = 0;
    rolltui_mem_stats(nullptr, nullptr, nullptr, &live_before, nullptr, nullptr);
    const bool refused = d.parse(kBad, sizeof kBad - 1, nullptr) == nullptr;
    rolltui_mem_stats(nullptr, nullptr, nullptr, &live_after, nullptr, nullptr);
    check(refused, "a NULL report is legal on the failing path too: bad text is refused, not a crash");
    check(live_after == live_before,
          "…and the report it had nowhere to put is released: the allocator is back to baseline");
  }

  return report("rolltui presets_test");
}
