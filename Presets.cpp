// rolltui/Presets.cpp — see Presets.hpp and PresetStore.hpp.
#include "rolltui/Presets.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <unistd.h>

#include "rolltui/c/rolltui_embedded.h"
#include "rolltui/c/rolltui_presets.h"

namespace rolltui {


namespace fs = std::filesystem;
using json::Value;

// ---- files ----------------------------------------------------------------------------

// THE FILE WORK IS BEHIND THE BOUNDARY (`rolltui/c/rolltui_presets.h`) since Phase 15 m3 —
// it is mechanics, not domain knowledge, and the store needs it in whichever language the
// store is written in. What is left here is the C++ shapes: a `std::string` out-parameter
// and a `std::vector<std::string>`, filled through the boundary's one `put` callback.
namespace preset_files {

namespace {
void put_string(void* ctx, const char* s, std::size_t len) { static_cast<std::string*>(ctx)->append(s, len); }
}  // namespace

bool read_file(const std::string& path, std::string& out) {
  out.clear();
  return rolltui_preset_read_file(path.data(), path.size(), put_string, &out) != 0;
}

bool write_file_atomic(const std::string& path, const std::string& bytes, std::string& error) {
  error.clear();
  return rolltui_preset_write_file_atomic(path.data(), path.size(), bytes.data(), bytes.size(), put_string, &error) != 0;
}

std::vector<std::string> json_names_in(const std::string& dir) {
  std::vector<std::string> out;
  rolltui_preset_json_names_in(dir.data(), dir.size(),
                               [](void* ctx, const char* s, std::size_t len) {
                                 static_cast<std::vector<std::string>*>(ctx)->emplace_back(s, len);
                               },
                               &out);
  return out;
}

bool looks_like_path(std::string_view s) { return rolltui_preset_looks_like_path(s.data(), s.size()) != 0; }

bool valid_preset_name(std::string_view name) { return rolltui_preset_valid_name(name.data(), name.size()) != 0; }

}  // namespace preset_files

std::string PresetLoadReport::summary() const {
  if (clean()) return "";
  if (!error.empty()) return error;
  std::string s;
  auto add = [&](const std::string& x) { if (!s.empty()) s += "; "; s += x; };
  for (const std::string& b : bad_values) add("bad: " + b);
  for (const std::string& u : unknown_keys) add("unknown: " + u);
  if (!colours.error.empty()) add("colours: " + colours.error);
  if (!colours.missing_roles.empty()) add("colours: " + std::to_string(colours.missing_roles.size()) + " roles missing (inherit text)");
  for (const std::string& b : colours.bad_values) add("colours: " + b);
  for (const std::string& u : colours.unknown_keys) add("colours: unknown " + u);
  if (!layout.error.empty()) add("layout: " + layout.error);
  for (const std::string& b : layout.bad_values) add("layout: " + b);
  for (const std::string& u : layout.unknown_keys) add("layout: unknown " + u);
  if (!bindings.clean()) add("bindings: " + bindings.summary());
  return s;
}

std::string_view to_string(SaveResult r) {
  switch (r) {
    case SaveResult::Saved: return "saved";
    case SaveResult::RefusedShipped: return "refused: a shipped preset is read-only";
    case SaveResult::ExistsAsk: return "a preset with that name exists; confirm to overwrite";
    case SaveResult::BadName: return "not a preset name (letters, digits, - _ . ; not starting with a dot)";
    case SaveResult::WriteFailed: return "write failed";
  }
  return "";
}

// ---- the Theme domain: file format ------------------------------------------------------

bool valid_mode_setting(std::string_view s) { return s == "auto" || s == "dark" || s == "light"; }
bool valid_depth_setting(std::string_view s) { return s == "auto" || s == "truecolor" || s == "256" || s == "16" || s == "mono"; }

std::optional<ThemeMode> mode_from_setting(std::string_view s) {
  if (s == "dark") return ThemeMode::Dark;
  if (s == "light") return ThemeMode::Light;
  return std::nullopt;
}

std::optional<ColorDepth> depth_from_setting(std::string_view s) {
  if (s == "truecolor") return ColorDepth::TrueColor;
  if (s == "256") return ColorDepth::Ansi256;
  if (s == "16") return ColorDepth::Ansi16;
  if (s == "mono") return ColorDepth::Mono;
  return std::nullopt;
}

// ---- the Theme domain: PARSING/DUMPING PORTED TO C (`rolltui/c/rolltui_presets.h`, this
// task) ------------------------------------------------------------------------------------
//
// What moved: the structural walk (which keys exist, "colours" required, "layout" ignored
// with a note, unknown keys reported) and the colours part's validation, now calling
// `rolltui_theme_load` DIRECTLY — the entanglement that used to force this to stay C++
// (validation went through this file's own `load_theme`, which was C++) is gone now that
// `load_theme`'s actual algorithm is `rolltui_theme_load` (Phase 15 m5, `rolltui/c/
// rolltui_theme.h`). What did NOT move, and why (both are `rolltui_presets.h`'s own header
// comment, restated briefly since this is the call site it matters at):
//   - the VOCAB table (`theme_vocab()` below) — a C file may not build one (`rolltui_style.h`:
//     "a C file names no role"), so it crosses as a parameter, same as it already does for
//     `rolltui_theme_load` itself.
//   - `valid_mode_setting`/`valid_depth_setting` just above — re-deriving that five-line
//     vocabulary in C would be a THIRD spelling of it (`kSettings`' help text below is
//     already a tolerated second one) for a predicate with no other caller anywhere in the
//     tree, so `rolltui_theme_preset_parse` takes it as a CALLBACK instead — the same "domain
//     supplies the policy" shape `RolltuiPresetDomain` itself is built from — which keeps the
//     walk a single pass in file order rather than a second pass over the object.
namespace {

// Bridges to `RolltuiThemePresetValidFn` (`int(*)(const char*, size_t)`, no context — both
// predicates are pure over a string with nothing to capture). Plain free functions rather than
// captureless lambdas only because there are two named call sites for each; the C++-to-C
// function-pointer crossing is the same one `PresetStore.hpp`'s own `domain_storage<D>()`
// already relies on throughout.
int mode_valid_c(const char* s, std::size_t len) { return valid_mode_setting(std::string_view(s, len)) ? 1 : 0; }
int depth_valid_c(const char* s, std::size_t len) { return valid_depth_setting(std::string_view(s, len)) ? 1 : 0; }

// The Theme-relevant fields of `RolltuiThemePresetReport`, copied into a `PresetLoadReport` —
// the C side's report and the C++ one agree field for field, so this is a straight copy, not
// a translation. Shared by `theme_preset_from_json` and `ThemeDomain::parse_partial`.
void copy_theme_preset_report(const RolltuiThemePresetReport& rep, PresetLoadReport& report) {
  report.error = rep.error.str();
  for (std::size_t i = 0; i < rep.bad_values_n; ++i) report.bad_values.push_back(rep.bad_values[i].str());
  for (std::size_t i = 0; i < rep.unknown_keys_n; ++i) report.unknown_keys.push_back(rep.unknown_keys[i].str());
  for (std::size_t i = 0; i < rep.notes_n; ++i) report.notes.push_back(rep.notes[i].str());
  report.colours.error = rep.colours.error.str();
  for (std::size_t i = 0; i < rep.colours.missing_roles_n; ++i) report.colours.missing_roles.push_back(rep.colours.missing_roles[i].str());
  for (std::size_t i = 0; i < rep.colours.unknown_keys_n; ++i) report.colours.unknown_keys.push_back(rep.colours.unknown_keys[i].str());
  for (std::size_t i = 0; i < rep.colours.bad_values_n; ++i) report.colours.bad_values.push_back(rep.colours.bad_values[i].str());
}

}  // namespace

// `Theme.cpp`'s vocab table, given external linkage there for exactly this: one table, read
// in two files, built in neither a second time. NOT declared in `Theme.hpp` — see that file's
// header comment — so this is the same borrowed-declaration move made three lines down for
// `json::value_to_c`/`value_from_c` (and that `Theme.cpp` itself already makes for those two).
const RolltuiThemeVocab& theme_vocab();

// Needed for exactly two things: handing "colours"/the whole preset object to the C parser
// when this file already holds a parsed `json::Value` (`PresetStore.hpp`'s adapter parses
// text to a tree before calling this domain), and reading the "colours" subtree back out as a
// `json::Value` afterwards, which is that field's own C++ shape (`ThemePreset::colours`,
// declared in Presets.hpp — see the comment there for why it stays that shape and which
// callers force it). Not declared in `Json.hpp`: see `Theme.cpp`'s identical note.
namespace json {
RolltuiJsonValue* value_to_c(const Value& v);
Value value_from_c(const RolltuiJsonValue* v);
}  // namespace json

std::optional<ThemePreset> theme_preset_from_json(const Value& v, PresetLoadReport& report) {
  report = PresetLoadReport{};
  RolltuiJsonValue* root_c = json::value_to_c(v);
  RolltuiStr mode{}, depth{};
  const RolltuiJsonValue* colours_c = nullptr;
  RolltuiThemePresetReport rep{};
  const int ok = rolltui_theme_preset_parse(root_c, &theme_vocab(), mode_valid_c, depth_valid_c, &mode, &depth,
                                            &colours_c, &rep);
  copy_theme_preset_report(rep, report);
  std::optional<ThemePreset> out;
  if (ok) {
    ThemePreset p;
    p.mode = mode.str();
    p.depth = depth.str();
    // `colours_c` is a BORROW of a subtree of `root_c` (this header's own comment) and
    // `root_c` is freed below, so `p.colours` clones it rather than adopting it.
    p.colours.reset(rolltui_json_clone(colours_c));
    out = std::move(p);
  }
  rolltui_theme_preset_report_release(&rep);
  rolltui_str_free(&mode);
  rolltui_str_free(&depth);
  rolltui_json_free(root_c);
  return out;
}

Value theme_preset_to_json(const ThemePreset& p, std::string_view name) {
  // `rolltui_theme_preset_to_json` TAKES OWNERSHIP of the colours tree; `p` keeps its own, so
  // this hands over a clone rather than `p.colours` itself.
  RolltuiJsonValue* c = rolltui_theme_preset_to_json(rolltui_json_clone(p.colours.get()), p.mode.data(), p.mode.size(),
                                                     p.depth.data(), p.depth.size(), name.data(), name.size());
  Value out = json::value_from_c(c);
  rolltui_json_free(c);
  return out;
}

std::optional<Theme> resolve_colours(const ThemePreset& p, ThemeMode mode, ThemeLoadReport& report) {
  return load_theme(p.colours.get(), mode, report);
}

// ---- the Theme domain: traits and the store's own methods -------------------------------

std::size_t ThemeDomain::shipped_count() { return rolltui_kThemePresetCount; }
std::pair<std::string_view, std::string_view> ThemeDomain::shipped_at(std::size_t i) { return {rolltui_kThemePresets[i].name, rolltui_kThemePresets[i].text}; }

std::optional<ThemePreset> ThemeDomain::parse_partial(const json::Value& v, const ThemePreset& working, PresetLoadReport& report) {
  RolltuiJsonValue* root_c = json::value_to_c(v);
  const RolltuiJsonValue* colours_c = nullptr;
  RolltuiThemePresetReport rep{};
  const int ok = rolltui_theme_preset_parse_partial(root_c, &theme_vocab(), &colours_c, &rep);
  copy_theme_preset_report(rep, report);
  std::optional<ThemePreset> out;
  if (ok) {
    ThemePreset p = working;
    // `colours_c` borrows `root_c` (see `rolltui_theme_preset_parse_partial`'s own comment),
    // which is freed below.
    p.colours.reset(rolltui_json_clone(colours_c));
    out = std::move(p);
  }
  rolltui_theme_preset_report_release(&rep);
  rolltui_json_free(root_c);
  return out;
}

// ---- the Layout domain (Phase 10 m1) ----------------------------------------------------

std::size_t LayoutDomain::shipped_count() { return rolltui_kLayoutPresetCount; }
std::pair<std::string_view, std::string_view> LayoutDomain::shipped_at(std::size_t i) { return {rolltui_kLayoutPresets[i].name, rolltui_kLayoutPresets[i].text}; }

std::optional<Layout> LayoutDomain::parse(const json::Value& v, PresetLoadReport& report) {
  report = PresetLoadReport{};
  if (!v.is_object()) { report.error = "a layout file must be a JSON object"; return std::nullopt; }
  // "preset" is the store's own bookkeeping in a working copy, not a layout key; strip
  // it before the layout loader sees it, or every start would report it as unknown.
  json::Value body = v;
  body.obj.erase(std::remove_if(body.obj.begin(), body.obj.end(), [](const auto& kv) { return kv.first == "preset"; }), body.obj.end());
  std::optional<Layout> l = load_layout(body, report.layout);
  if (!l) { report.error = report.layout.error; return std::nullopt; }
  // A Phase 9 layout's slot names were rewritten to kind[:source] (Layout.hpp): say so
  // once, the way the theme→layout migration does. The file itself is rewritten by the
  // next autosave, so this is said until the user's own copy is in the new form.
  for (const std::string& m : report.layout.migrated) report.notes.push_back("layout: content " + m);
  return l;
}

// The preset's name is deliberately NOT written over the layout's own "name": a save
// that quietly changes what was saved is the silent-loss shape this whole split is
// trying to avoid, and the working copy would then not round-trip (the file would come
// back with a different name than the one in memory, so `modified()` would lie). A
// layout's name is what it calls itself; the preset name is what the store filed it
// under, and the two are allowed to differ — Phase 9's layouts/two.json holding a
// layout named "no-panel" already did.
json::Value LayoutDomain::to_json(const Layout& l, std::string_view) { return layout_to_json_value(l); }

// ---- the Phase 9 → Phase 10 migration ---------------------------------------------------

MigrationReport migrate_theme_layout(const std::string& dir) {
  MigrationReport out;
  const std::string theme_path = dir + "/" + std::string(ThemeDomain::working_file);
  const std::string layout_path = dir + "/" + std::string(LayoutDomain::working_file);
  std::string text;
  if (!preset_files::read_file(theme_path, text)) return out;  // a fresh install: nothing to move
  std::string err;
  Value v = json::parse(text, err);
  if (!err.empty() || !v.is_object() || !v.has("layout")) return out;  // unreadable, or already Phase 10

  // Copy first, strip second, and only strip once the copy is safely on disk — the
  // other order loses the layout if the write fails.
  std::error_code ec;
  if (fs::exists(layout_path, ec)) {
    out.notes.push_back("the theme working copy still carried a layout; " + layout_path + " already exists, so it was left alone");
  } else {
    LayoutLoadReport lrep;
    std::optional<Layout> l = load_layout(v.get("layout"), lrep);
    if (!l) {
      out.error = theme_path + ": its \"layout\" part is unusable (" + lrep.error + "); it was left in place";
      return out;
    }
    const std::string origin = l->name.empty() ? "default" : l->name.str();
    Value lv = LayoutDomain::to_json(*l, origin);
    lv.set("preset", Value::string(origin));
    if (!preset_files::write_file_atomic(layout_path, json::dump(lv, 2) + "\n", out.error)) return out;
    out.moved = true;
    out.layout_name = origin;
    out.notes.push_back("moved the layout '" + origin + "' out of the theme working copy into " + layout_path + " (it is its own preset domain now)");
  }
  v.obj.erase(std::remove_if(v.obj.begin(), v.obj.end(), [](const auto& kv) { return kv.first == "layout"; }), v.obj.end());
  std::string werr;
  if (!preset_files::write_file_atomic(theme_path, json::dump(v, 2) + "\n", werr)) {
    // The layout is already safe; say what did not happen rather than claim success.
    out.notes.push_back("could not rewrite " + theme_path + " without its layout part (" + werr + "); it is ignored on load");
    return out;
  }
  out.rewrote_theme = true;
  return out;
}

// ---- the Bindings domain -------------------------------------------------------------------

std::size_t BindingsDomain::shipped_count() { return rolltui_kBindingsPresetCount; }
std::pair<std::string_view, std::string_view> BindingsDomain::shipped_at(std::size_t i) { return {rolltui_kBindingsPresets[i].name, rolltui_kBindingsPresets[i].text}; }

std::optional<Bindings> BindingsDomain::parse(const json::Value& v, PresetLoadReport& report) {
  report = PresetLoadReport{};
  std::optional<Bindings> b = Bindings::from_json(v, report.bindings);
  if (!b) { report.error = report.bindings.error; return std::nullopt; }
  for (const auto& [k, x] : v.obj)
    if (k != "name" && k != "bindings" && k != "preset") report.unknown_keys.push_back(k);
  report.bindings.unknown_keys.clear();  // reported once, above
  // An action this library renamed, rewritten once by the loader (Phase 11 m2): say so
  // in words, exactly as the Layout domain says a rewritten content. Not a problem — the
  // file loaded and every chord in it is live — so it is a note, and the next autosave
  // writes the new name.
  for (const std::string& m : report.bindings.migrated) report.notes.push_back("bindings: action " + m);
  return b;
}

// ---- precedence (Phase 17 m2: moved to `rolltui/c/rolltui_presets.h`'s own "settings and
// precedence" section — see that header for the boundary and why `working_value` stays here
// regardless) ---------------------------------------------------------------------------------

std::string_view rung_name(Rung r) {
  std::size_t len = 0;
  const char* p = rolltui_preset_rung_name(static_cast<RolltuiPresetRung>(r), &len);
  return {p, len};
}

Resolved resolve_setting(std::string_view flag, std::string_view env, std::string_view working, std::string_view builtin) {
  const char* value = nullptr;
  std::size_t value_len = 0;
  RolltuiPresetRung rung = ROLLTUI_PRESET_RUNG_BUILTIN;
  rolltui_preset_resolve_setting(flag.data(), flag.size(), env.data(), env.size(), working.data(), working.size(),
                                 builtin.data(), builtin.size(), &value, &value_len, &rung);
  // `value`/`value_len` BORROW one of the four arguments above (rolltui_presets.h); copied
  // into `Resolved::value` here because that field is OWNING, exactly as it was before.
  return {std::string(value, value_len), static_cast<Rung>(rung)};
}

std::string_view domain_name(Domain d) {
  std::size_t len = 0;
  const char* p = rolltui_preset_domain_name(static_cast<RolltuiPresetDomainId>(d), &len);
  return {p, len};
}

namespace {
std::vector<SettingSpec> build_settings() {
  std::vector<SettingSpec> out;
  const std::size_t n = rolltui_preset_settings_count();
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const RolltuiPresetSettingSpec& s = *rolltui_preset_settings_at(i);
    out.push_back({std::string_view(s.key, s.key_len), static_cast<Domain>(s.domain),
                    std::string_view(s.env_suffix, s.env_suffix_len), std::string_view(s.builtin, s.builtin_len),
                    std::string_view(s.values, s.values_len)});
  }
  return out;
}
}  // namespace

// Every field BORROWS a `rolltui_preset_settings_at()` string literal (static storage
// duration, alive for the process's whole life), so this vector's own elements are safe to
// hand out `string_view`s into indefinitely — the same lifetime the old `inline constexpr`
// array gave them, just built once at startup instead of spelled out a second time here.
const std::vector<SettingSpec> kSettings = build_settings();

const SettingSpec* setting(std::string_view key) {
  const int i = rolltui_preset_setting_index(key.data(), key.size());
  return i < 0 ? nullptr : &kSettings[static_cast<std::size_t>(i)];
}

std::string working_value(const ThemePresets& store, std::string_view key) {
  if (key == domain_name(Domain::Theme)) return store.origin();
  const ThemePreset w = store.working();
  if (key == "theme_mode") return w.mode;
  if (key == "color_depth") return w.depth;
  return "";
}

std::string working_value(const LayoutPresets& store, std::string_view key) {
  if (key == domain_name(Domain::Layout)) return store.origin();
  return "";
}

std::string working_value(const BindingsPresets& store, std::string_view key) {
  if (key == domain_name(Domain::Bindings)) return store.origin();
  return "";
}

}  // namespace rolltui
