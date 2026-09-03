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

namespace rolltui {

namespace embedded {
extern const std::pair<std::string_view, std::string_view> kThemePresets[];
extern const std::size_t kThemePresetCount;
extern const std::pair<std::string_view, std::string_view> kLayoutPresets[];
extern const std::size_t kLayoutPresetCount;
extern const std::pair<std::string_view, std::string_view> kBindingsPresets[];
extern const std::size_t kBindingsPresetCount;
}  // namespace embedded

namespace fs = std::filesystem;
using json::Value;

// ---- files ----------------------------------------------------------------------------

namespace preset_files {

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

// Write to a sibling temp file, then rename — a reader sees the old complete file or
// the new complete file, never a mix (the state-file rule from ResilientModelManager).
bool write_file_atomic(const std::string& path, const std::string& bytes, std::string& error) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  if (ec) { error = "cannot create " + fs::path(path).parent_path().string() + ": " + ec.message(); return false; }
  const std::string tmp = path + ".tmp." + std::to_string(::getpid());
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + tmp + ": " + std::strerror(errno); return false; }
    out << bytes;
    if (!out) { error = "short write to " + tmp; return false; }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    error = "cannot rename " + tmp + " to " + path + ": " + std::strerror(errno);
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

std::vector<std::string> json_names_in(const std::string& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
    if (!e.is_regular_file(ec)) continue;
    const fs::path p = e.path();
    if (p.extension() != ".json") continue;
    out.push_back(p.stem().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool looks_like_path(std::string_view s) {
  return s.find('/') != std::string_view::npos || (s.size() > 5 && s.substr(s.size() - 5) == ".json");
}

bool valid_preset_name(std::string_view name) {
  if (name.empty() || name.size() > 64 || name[0] == '.') return false;
  for (char c : name)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return false;
  return true;
}

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

std::optional<ThemePreset> theme_preset_from_json(const Value& v, PresetLoadReport& report) {
  report = PresetLoadReport{};
  if (!v.is_object()) { report.error = "a preset file must be a JSON object"; return std::nullopt; }
  if (!v.has("colours")) { report.error = "a preset file needs a \"colours\" object (Theme.hpp's format)"; return std::nullopt; }
  ThemePreset p;
  for (const auto& [k, x] : v.obj) {
    if (k == "name" || k == "preset") {
      if (!x.is_string()) report.bad_values.push_back(k + ": expected a string");
    } else if (k == "mode") {
      if (!x.is_string() || !valid_mode_setting(x.str)) report.bad_values.push_back("mode: expected auto | dark | light");
      else p.mode = x.str;
    } else if (k == "depth") {
      if (!x.is_string() || !valid_depth_setting(x.str)) report.bad_values.push_back("depth: expected auto | truecolor | 256 | 16 | mono");
      else p.depth = x.str;
    } else if (k == "colours") {
      p.colours = x;
    } else if (k == "layout") {
      // A Phase 9 preset file. Not an unknown key and not a bad value — the part was
      // valid, it simply is not the Theme's any more — so it is a NOTE naming it, and
      // the file still loads clean. (The working copy is moved across once instead;
      // migrate_theme_layout().)
      report.notes.push_back("\"layout\": ignored — a layout is its own preset now (layouts/)");
    } else {
      report.unknown_keys.push_back(k);
    }
  }
  // Validate the colours part at both modes so a bad file is reported at load, not at
  // the first frame; the loader's own report is kept (missing roles, unknown keys).
  ThemeLoadReport dark, light;
  std::optional<Theme> td = load_theme(p.colours, ThemeMode::Dark, dark);
  load_theme(p.colours, ThemeMode::Light, light);
  report.colours = dark;
  for (const std::string& b : light.bad_values)
    if (std::find(dark.bad_values.begin(), dark.bad_values.end(), b) == dark.bad_values.end()) report.colours.bad_values.push_back(b);
  if (!td) { report.error = "colours: " + dark.error; return std::nullopt; }
  return p;
}

Value theme_preset_to_json(const ThemePreset& p, std::string_view name) {
  Value o = Value::object();
  o.set("name", Value::string(std::string(name)));
  o.set("mode", Value::string(p.mode));
  o.set("depth", Value::string(p.depth));
  o.set("colours", p.colours);
  return o;
}

std::optional<Theme> resolve_colours(const ThemePreset& p, ThemeMode mode, ThemeLoadReport& report) {
  return load_theme(p.colours, mode, report);
}

// ---- the Theme domain: traits and the store's own methods -------------------------------

std::size_t ThemeDomain::shipped_count() { return embedded::kThemePresetCount; }
std::pair<std::string_view, std::string_view> ThemeDomain::shipped_at(std::size_t i) { return embedded::kThemePresets[i]; }

std::optional<ThemePreset> ThemeDomain::parse_partial(const json::Value& v, const ThemePreset& working, PresetLoadReport& report) {
  if (!v.is_object() || v.has("colours") || !v.has("roles")) return std::nullopt;
  ThemePreset p = working;
  p.colours = v;
  ThemeLoadReport tr;
  if (!load_theme(v, ThemeMode::Dark, tr)) { report.error = tr.error; return std::nullopt; }
  report.colours = tr;
  report.notes.push_back("is a colours-only theme file; mode and depth are kept");
  return p;
}

// ---- the Layout domain (Phase 10 m1) ----------------------------------------------------

std::size_t LayoutDomain::shipped_count() { return embedded::kLayoutPresetCount; }
std::pair<std::string_view, std::string_view> LayoutDomain::shipped_at(std::size_t i) { return embedded::kLayoutPresets[i]; }

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
    const std::string origin = l->name.empty() ? "default" : l->name;
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

std::size_t BindingsDomain::shipped_count() { return embedded::kBindingsPresetCount; }
std::pair<std::string_view, std::string_view> BindingsDomain::shipped_at(std::size_t i) { return embedded::kBindingsPresets[i]; }

std::optional<Bindings> BindingsDomain::parse(const json::Value& v, PresetLoadReport& report) {
  report = PresetLoadReport{};
  std::optional<Bindings> b = Bindings::from_json(v, report.bindings);
  if (!b) { report.error = report.bindings.error; return std::nullopt; }
  for (const auto& [k, x] : v.obj)
    if (k != "name" && k != "bindings" && k != "preset") report.unknown_keys.push_back(k);
  report.bindings.unknown_keys.clear();  // reported once, above
  return b;
}

// ---- precedence -----------------------------------------------------------------------------------

std::string_view rung_name(Rung r) {
  switch (r) {
    case Rung::Flag: return "flag";
    case Rung::Env: return "environment";
    case Rung::Working: return "working copy";
    case Rung::Builtin: return "built-in default";
  }
  return "";
}

Resolved resolve_setting(std::string_view flag, std::string_view env, std::string_view working, std::string_view builtin) {
  if (!flag.empty()) return {std::string(flag), Rung::Flag};
  if (!env.empty()) return {std::string(env), Rung::Env};
  if (!working.empty()) return {std::string(working), Rung::Working};
  return {std::string(builtin), Rung::Builtin};
}

std::string_view domain_name(Domain d) {
  switch (d) {
    case Domain::Theme: return "theme";
    case Domain::Layout: return "layout";
    case Domain::Bindings: return "bindings";
  }
  return "";
}

const SettingSpec* setting(std::string_view key) {
  for (const SettingSpec& s : kSettings)
    if (s.key == key) return &s;
  return nullptr;
}

std::string working_value(const ThemePresets& store, std::string_view key) {
  if (key == "theme") return store.origin();
  const ThemePreset w = store.working();
  if (key == "theme_mode") return w.mode;
  if (key == "color_depth") return w.depth;
  return "";
}

std::string working_value(const LayoutPresets& store, std::string_view key) {
  if (key == "layout") return store.origin();
  return "";
}

std::string working_value(const BindingsPresets& store, std::string_view key) {
  if (key == "bindings") return store.origin();
  return "";
}

}  // namespace rolltui
