//
// presets_test.cpp — the preset system (milestone 11 / 11e) on the Theme domain: the
// five rules in Presets.hpp, the shipped files against the built-in themes and layout,
// the four-rung precedence table (every combination of present/absent rungs), the
// file format's report, layout-file discovery, and the OSC 11 reply parser with the
// light/dark rule. Runs in a scratch directory under $TMPDIR it creates and removes.
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
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;
namespace fs = std::filesystem;

#ifndef ROLLTUI_PRESETS_DIR
#error "ROLLTUI_PRESETS_DIR must point at rolltui/presets/themes"
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
    check(d->layout == *builtin_layout("default") && d->mode == "auto" && d->depth == "auto", "shipped 'default' carries the default layout, mode auto, depth auto");
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
    store.set_layout(*builtin_layout("stacked"));
    check(store.version() > v0 && store.label() == "default (modified)" && store.modified(), "a layout change bumps the version and the label reads 'default (modified)'");
    check(fs::exists(store.working_path()), "…and autosaved " + store.working_path());
    std::string err;
    json::Value v = json::parse(read_file(store.working_path()), err);
    check(err.empty() && v.get("preset").as_string() == "default" && v.get("layout").get("name").as_string() == "stacked",
          "the working file records its origin preset and the whole domain");
    check(fs::directory_iterator(dir) != fs::directory_iterator() && !fs::exists(store.working_path() + ".tmp." + std::to_string(::getpid())), "no temp file is left behind (written by rename)");
    store.set_layout(*builtin_layout("default"));
    check(store.label() == "default" && !store.modified(), "putting the layout back makes it 'default' again — identity is by comparison, not a dirty flag");
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
    write_file(fs::path(bad) / "theme.working.json", R"({"mode":"sideways","depth":"auto","colours":{"roles":{"text":{"fg":"none"}}},"layout":{"root":{"content":"transcript"}},"extra":1})");
    ThemePresets s4({bad, false, ""});
    rep = s4.start();
    check(rep.error.empty() && rep.bad_values.size() == 1 && rep.bad_values[0].find("mode") == 0 && rep.unknown_keys.size() == 1 && rep.unknown_keys[0] == "extra" &&
              rep.colours.missing_roles.size() == kRoleCount - 1,
          "a loadable working copy with problems loads and reports each: bad mode, unknown key, missing roles [" + rep.summary() + "]");
    check(s4.working().mode == "auto" && s4.working().layout.base.root.content == "transcript", "the bad value keeps its default; the good parts load");
  }
  // ---- colours-only theme files and layout files ----
  {
    const std::string d2 = (world / "files").string();
    ThemePresets s({d2, false, ""});
    s.start();
    write_file(fs::path(d2) / "colours.json", theme_to_json(*builtin_theme("mono")));
    PresetLoadReport rep;
    check(s.load((fs::path(d2) / "colours.json").string(), rep) && !rep.notes.empty() && rep.notes.back().find("colours-only") != std::string::npos,
          "a colours-only theme file loads into the colours part with a note");
    ThemeLoadReport tr;
    check(resolve_colours(s.working(), ThemeMode::Dark, tr)->styles == builtin_theme("mono")->styles && s.working().layout == *builtin_layout("default") && s.label() == "default (modified)",
          "…the look is mono, the layout kept, and the label says modified against 'default'");
    write_file(fs::path(d2) / "layouts" / "two.json", layout_to_json(*builtin_layout("no-panel")));
    check(s.layout_files() == std::vector<std::string>{"two"}, "layout files in <dir>/layouts are discovered by name");
    LayoutLoadReport lr;
    std::optional<Layout> l = s.find_layout("two", lr);
    check(l && lr.clean() && l->name == "no-panel", "find_layout resolves a layouts/ name");
    check(s.find_layout("stacked", lr) && s.find_layout("stacked", lr)->name == "stacked", "…and a built-in name");
    check(!s.find_layout("nothing", lr) && lr.error.find("no layout 'nothing'") == 0, "…and names the failure otherwise");
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
    json::Value nl = theme_preset_to_json(*d, "x");
    nl.obj.erase(std::remove_if(nl.obj.begin(), nl.obj.end(), [](auto& kv) { return kv.first == "layout"; }), nl.obj.end());
    back = theme_preset_from_json(nl, rep);
    check(back && back->layout == *builtin_layout("default") && !rep.notes.empty(), "a preset without a layout part gets the default layout and a note");
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
    check(theme_setting("theme") && theme_setting("layout") && theme_setting("theme_mode") && theme_setting("color_depth") && !theme_setting("frontend"),
          "the table holds exactly the Theme domain's four settings (frontend is a host's)");
    check(theme_setting("theme")->builtin == "default" && theme_setting("theme_mode")->builtin == "auto" && theme_setting("color_depth")->builtin == "auto",
          "built-in defaults: default / auto / auto");
    PresetLoadReport lrep;
    store.load("default", lrep);  // (the store from above)
    store.set_layout(*builtin_layout("panel-left"));
    store.set_mode("dark");
    check(working_value(store, "theme") == "default" && working_value(store, "layout") == "panel-left" && working_value(store, "theme_mode") == "dark" &&
              working_value(store, "color_depth") == "auto",
          "working_value reads each setting from the working copy");
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
    check(working_value(bs, "bindings") == "bad" && bindings_setting("bindings") && bindings_setting("bindings")->builtin == "default" && !bindings_setting("theme"),
          "the Bindings domain's one setting: 'bindings', built-in 'default'");
    // Both domains in one directory, two working files, neither touching the other.
    ThemePresets ts({bdir, false, ""});
    ts.start();
    ts.set_mode("light");
    check(fs::exists(fs::path(bdir) / "theme.working.json") && fs::exists(fs::path(bdir) / "bindings.working.json") && bs.label() == "bad" && ts.label() == "default (modified)",
          "a session is Theme X + Bindings Y: two working copies side by side, each with its own label");
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
  return report("rolltui presets_test");
}
