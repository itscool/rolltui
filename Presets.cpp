// rolltui/Presets.cpp — see Presets.hpp.
#include "rolltui/Presets.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
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
}  // namespace embedded

namespace fs = std::filesystem;
using json::Value;

// ---- files ----------------------------------------------------------------------------

namespace {

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

}  // namespace

// ---- the Theme domain: file format ------------------------------------------------------

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
  return s;
}

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
  bool have_layout = false;
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
      std::optional<Layout> l = load_layout(x, report.layout);
      if (l) { p.layout = *l; have_layout = true; }
    } else {
      report.unknown_keys.push_back(k);
    }
  }
  if (!have_layout) {
    p.layout = *builtin_layout("default");
    if (!v.has("layout")) report.notes.push_back("no \"layout\" part; the built-in default layout is used");
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
  o.set("layout", layout_to_json_value(p.layout));
  return o;
}

std::optional<Theme> resolve_colours(const ThemePreset& p, ThemeMode mode, ThemeLoadReport& report) {
  return load_theme(p.colours, mode, report);
}

// ---- shipped ----------------------------------------------------------------------------------

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

std::vector<std::string_view> ThemePresets::shipped_names() {
  std::vector<std::string_view> out;
  // "default" first, then the rest in the order they were embedded (alphabetical).
  for (std::size_t i = 0; i < embedded::kThemePresetCount; ++i)
    if (embedded::kThemePresets[i].first == "default") out.push_back(embedded::kThemePresets[i].first);
  for (std::size_t i = 0; i < embedded::kThemePresetCount; ++i)
    if (embedded::kThemePresets[i].first != "default") out.push_back(embedded::kThemePresets[i].first);
  return out;
}

bool ThemePresets::is_shipped(std::string_view name) {
  for (std::size_t i = 0; i < embedded::kThemePresetCount; ++i)
    if (embedded::kThemePresets[i].first == name) return true;
  return false;
}

std::string_view ThemePresets::shipped_json(std::string_view name) {
  for (std::size_t i = 0; i < embedded::kThemePresetCount; ++i)
    if (embedded::kThemePresets[i].first == name) return embedded::kThemePresets[i].second;
  return "";
}

const ThemePreset* ThemePresets::shipped(std::string_view name) {
  static const std::vector<std::pair<std::string, ThemePreset>> cache = [] {
    std::vector<std::pair<std::string, ThemePreset>> out;
    bool have_default = false;
    for (std::size_t i = 0; i < embedded::kThemePresetCount; ++i) {
      std::string err;
      Value v = json::parse(embedded::kThemePresets[i].second, err);
      PresetLoadReport rep;
      std::optional<ThemePreset> p = err.empty() ? theme_preset_from_json(v, rep) : std::nullopt;
      if (!p || !rep.clean()) {
        // A shipped preset that does not load cleanly is a programming error (the
        // layout loader's standard); say so and stop rather than run half a theme.
        std::fprintf(stderr, "rolltui: shipped preset '%.*s' is broken: %s\n", static_cast<int>(embedded::kThemePresets[i].first.size()),
                     embedded::kThemePresets[i].first.data(), err.empty() ? rep.summary().c_str() : err.c_str());
        std::abort();
      }
      have_default |= embedded::kThemePresets[i].first == "default";
      out.emplace_back(std::string(embedded::kThemePresets[i].first), std::move(*p));
    }
    if (!have_default) { std::fprintf(stderr, "rolltui: no shipped preset named 'default' (rule 5)\n"); std::abort(); }
    return out;
  }();
  for (const auto& [n, p] : cache)
    if (n == name) return &p;
  return nullptr;
}

// ---- the store -----------------------------------------------------------------------------------

ThemePresets::ThemePresets(Options o) : opt_(std::move(o)) {
  working_ = *shipped("default");
  origin_ = "default";
  origin_content_ = working_;
}

std::string ThemePresets::working_path() const { return opt_.dir + "/theme.working.json"; }
std::string ThemePresets::preset_path(std::string_view name) const { return opt_.dir + "/themes/" + std::string(name) + ".json"; }

PresetLoadReport ThemePresets::start() {
  std::lock_guard<std::mutex> lock(mu_);
  PresetLoadReport rep;
  std::string text;
  if (!read_file(working_path(), text)) {
    rep.notes.push_back("no working copy at " + working_path() + "; started from the shipped 'default'");
    return rep;
  }
  std::string err;
  Value v = json::parse(text, err);
  if (!err.empty()) {
    rep.error = "working copy " + working_path() + " unreadable (" + err + "); started from the shipped 'default'";
    return rep;
  }
  std::optional<ThemePreset> p = theme_preset_from_json(v, rep);
  if (!p) {
    rep.error = "working copy " + working_path() + ": " + rep.error + "; started from the shipped 'default'";
    return rep;
  }
  working_ = std::move(*p);
  origin_ = std::string(v.get("preset").as_string("default"));
  // The origin's content, for the label: a shipped preset, a user file, or — when
  // the origin no longer exists — the working copy itself (so it reads as unmodified
  // rather than "(modified)" against nothing).
  PresetLoadReport ignore;
  std::optional<ThemePreset> oc = get_locked(origin_, ignore);
  origin_content_ = oc ? *oc : working_;
  if (!oc) rep.notes.push_back("the working copy's preset '" + origin_ + "' no longer exists");
  rep.notes.push_back("loaded the working copy (" + origin_ + (working_ == origin_content_ ? "" : " (modified)") + ")");
  ++version_;
  return rep;
}

ThemePreset ThemePresets::working() const { std::lock_guard<std::mutex> lock(mu_); return working_; }
std::string ThemePresets::origin() const { std::lock_guard<std::mutex> lock(mu_); return origin_; }
bool ThemePresets::modified() const { std::lock_guard<std::mutex> lock(mu_); return !(working_ == origin_content_); }
std::string ThemePresets::label() const {
  std::lock_guard<std::mutex> lock(mu_);
  return working_ == origin_content_ ? origin_ : origin_ + " (modified)";
}
std::uint64_t ThemePresets::version() const { std::lock_guard<std::mutex> lock(mu_); return version_; }
std::string ThemePresets::last_error() const { std::lock_guard<std::mutex> lock(mu_); return last_error_; }

bool ThemePresets::autosave_locked() {
  Value v = theme_preset_to_json(working_, origin_);
  v.set("preset", Value::string(origin_));
  std::string err;
  if (!write_file_atomic(working_path(), json::dump(v, 2) + "\n", err)) { last_error_ = err; return false; }
  last_error_.clear();
  return true;
}

void ThemePresets::touch(bool persist) {
  ++version_;
  if (persist) autosave_locked();
}

void ThemePresets::set_working(ThemePreset p, bool persist) {
  std::lock_guard<std::mutex> lock(mu_);
  working_ = std::move(p);
  touch(persist);
}
void ThemePresets::set_colours(Value colours, bool persist) {
  std::lock_guard<std::mutex> lock(mu_);
  working_.colours = std::move(colours);
  touch(persist);
}
void ThemePresets::set_layout(Layout l, bool persist) {
  std::lock_guard<std::mutex> lock(mu_);
  working_.layout = std::move(l);
  touch(persist);
}
void ThemePresets::set_mode(std::string mode, bool persist) {
  std::lock_guard<std::mutex> lock(mu_);
  working_.mode = std::move(mode);
  touch(persist);
}
void ThemePresets::set_depth(std::string depth, bool persist) {
  std::lock_guard<std::mutex> lock(mu_);
  working_.depth = std::move(depth);
  touch(persist);
}

std::vector<PresetInfo> ThemePresets::list() const {
  std::vector<PresetInfo> out;
  for (std::string_view n : shipped_names()) out.push_back({std::string(n), true, ""});
  for (const std::string& n : json_names_in(opt_.dir + "/themes"))
    if (!is_shipped(n)) out.push_back({n, false, preset_path(n)});
  return out;
}

std::optional<ThemePreset> ThemePresets::get_locked(std::string_view name_or_path, PresetLoadReport& report) const {
  report = PresetLoadReport{};
  if (const ThemePreset* s = shipped(name_or_path)) return *s;
  std::string path;
  if (looks_like_path(name_or_path)) path = std::string(name_or_path);
  else path = preset_path(name_or_path);
  std::string text;
  if (!read_file(path, text)) {
    report.error = "no preset '" + std::string(name_or_path) + "' (not shipped, and " + path + " is not readable)";
    return std::nullopt;
  }
  std::string err;
  Value v = json::parse(text, err);
  if (!err.empty()) { report.error = path + ": " + err; return std::nullopt; }
  if (v.is_object() && !v.has("colours") && v.has("roles")) {
    // A colours-only theme file: the working copy's other parts are kept.
    ThemePreset p = working_;
    p.colours = v;
    ThemeLoadReport tr;
    if (!load_theme(v, ThemeMode::Dark, tr)) { report.error = path + ": " + tr.error; return std::nullopt; }
    report.colours = tr;
    report.notes.push_back(path + " is a colours-only theme file; layout, mode and depth are kept");
    return p;
  }
  std::optional<ThemePreset> p = theme_preset_from_json(v, report);
  if (!p) report.error = path + ": " + report.error;
  return p;
}

std::optional<ThemePreset> ThemePresets::get(std::string_view name_or_path, PresetLoadReport& report) const {
  std::lock_guard<std::mutex> lock(mu_);
  return get_locked(name_or_path, report);
}

bool ThemePresets::load(std::string_view name_or_path, PresetLoadReport& report, bool persist) {
  std::lock_guard<std::mutex> lock(mu_);
  std::optional<ThemePreset> p = get_locked(name_or_path, report);
  if (!p) return false;
  const bool colours_only = !report.notes.empty() && report.notes.back().find("colours-only") != std::string::npos;
  working_ = std::move(*p);
  if (!colours_only) {
    origin_ = looks_like_path(name_or_path) ? fs::path(name_or_path).stem().string() : std::string(name_or_path);
    origin_content_ = working_;
  }
  touch(persist);
  return true;
}

SaveResult ThemePresets::save_as(std::string_view name, bool overwrite, std::string& error) {
  std::lock_guard<std::mutex> lock(mu_);
  error.clear();
  if (!valid_preset_name(name)) { error = std::string(to_string(SaveResult::BadName)); return SaveResult::BadName; }
  std::string path;
  if (is_shipped(name)) {
    if (!opt_.may_write_shipped) { error = std::string(to_string(SaveResult::RefusedShipped)); return SaveResult::RefusedShipped; }
    path = opt_.shipped_dir + "/" + std::string(name) + ".json";
  } else {
    path = preset_path(name);
    std::error_code ec;
    if (!overwrite && fs::exists(path, ec)) { error = std::string(to_string(SaveResult::ExistsAsk)); return SaveResult::ExistsAsk; }
  }
  const std::string bytes = json::dump(theme_preset_to_json(working_, name), 2) + "\n";
  if (!write_file_atomic(path, bytes, error)) return SaveResult::WriteFailed;
  origin_ = std::string(name);
  origin_content_ = working_;
  touch(true);
  return SaveResult::Saved;
}

std::vector<std::string> ThemePresets::layout_files() const { return json_names_in(opt_.dir + "/layouts"); }

std::optional<Layout> ThemePresets::find_layout(std::string_view name_or_path, LayoutLoadReport& report) const {
  report = LayoutLoadReport{};
  if (const Layout* b = builtin_layout(name_or_path)) return *b;
  const std::string path = looks_like_path(name_or_path) ? std::string(name_or_path) : opt_.dir + "/layouts/" + std::string(name_or_path) + ".json";
  std::string text;
  if (!read_file(path, text)) {
    report.error = "no layout '" + std::string(name_or_path) + "' (not built in, and " + path + " is not readable)";
    return std::nullopt;
  }
  std::optional<Layout> l = load_layout(text, report);
  if (!l) report.error = path + ": " + report.error;
  return l;
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

const SettingSpec* theme_setting(std::string_view key) {
  for (const SettingSpec& s : kThemeSettings)
    if (s.key == key) return &s;
  return nullptr;
}

std::string working_value(const ThemePresets& store, std::string_view key) {
  if (key == "theme") return store.origin();
  const ThemePreset w = store.working();
  if (key == "layout") return w.layout.name;
  if (key == "theme_mode") return w.mode;
  if (key == "color_depth") return w.depth;
  return "";
}

}  // namespace rolltui
