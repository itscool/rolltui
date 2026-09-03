#pragma once
//
// rolltui/PresetStore.hpp — the preset MECHANICS, once, for every domain (Presets.hpp
// states the five rules; this is their one implementation). A Domain is a traits type:
//
//   struct D {
//     using Value = ...;                                   // the whole domain, one unit
//     static constexpr std::string_view kind;              // "theme" | "layout" | "bindings" (messages)
//     static constexpr std::string_view working_file;      // "theme.working.json"
//     static constexpr std::string_view subdir;            // "themes" (user presets)
//     static std::size_t shipped_count();                  // the embedded table
//     static std::pair<std::string_view, std::string_view> shipped_at(std::size_t i);
//     static std::optional<Value> parse(const json::Value&, PresetLoadReport&);
//     // A PARTIAL file (a colours-only theme file): fills part of `working`, notes why;
//     // nullopt when the file is not partial (parse() is then used).
//     static std::optional<Value> parse_partial(const json::Value&, const Value& working, PresetLoadReport&);
//     static json::Value to_json(const Value&, std::string_view name);
//   };
// and Value must be ==-comparable (rule 4 is by comparison). Every method takes the
// store's lock and returns copies; `version()` bumps on every change.
//
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "rolltui/Json.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Bindings.hpp"

namespace rolltui {

struct PresetLoadReport {
  std::string error;                      // non-empty: unusable
  std::vector<std::string> unknown_keys;
  std::vector<std::string> bad_values;
  ThemeLoadReport colours;                // the colours part's own report (Theme domain)
  LayoutLoadReport layout;                // the Layout domain's own report
  BindingsLoadReport bindings;            // the Bindings domain's own report
  std::vector<std::string> notes;         // what happened, in words ("no working copy; started from default")
  bool clean() const {
    return error.empty() && unknown_keys.empty() && bad_values.empty() && colours.clean() && layout.clean() && bindings.clean();
  }
  std::string summary() const;            // one line, "" when clean
};

struct PresetInfo {
  std::string name;
  bool shipped = false;
  std::string path;  // "" for shipped
};

enum class SaveResult { Saved, RefusedShipped, ExistsAsk, BadName, WriteFailed };
std::string_view to_string(SaveResult r);

namespace preset_files {
bool read_file(const std::string& path, std::string& out);
bool write_file_atomic(const std::string& path, const std::string& bytes, std::string& error);
std::vector<std::string> json_names_in(const std::string& dir);
bool looks_like_path(std::string_view s);
bool valid_preset_name(std::string_view name);
}  // namespace preset_files

template <class D>
class PresetStore {
 public:
  using Value = typename D::Value;
  struct Options {
    std::string dir;                 // the host's rolltui directory
    bool may_write_shipped = false;  // the editor's privilege (rule 5)
    std::string shipped_dir;         // where shipped presets are written when allowed
  };
  explicit PresetStore(Options o) : opt_(std::move(o)) {
    working_ = *shipped("default");
    origin_ = "default";
    origin_content_ = working_;
  }

  // Startup: the autosaved working copy when present and loadable, else "default".
  PresetLoadReport start() {
    std::lock_guard<std::mutex> lock(mu_);
    PresetLoadReport rep;
    std::string text;
    if (!preset_files::read_file(working_path(), text)) {
      rep.notes.push_back("no working copy at " + working_path() + "; started from the shipped 'default'");
      return rep;
    }
    std::string err;
    json::Value v = json::parse(text, err);
    if (!err.empty()) {
      rep.error = "working copy " + working_path() + " unreadable (" + err + "); started from the shipped 'default'";
      return rep;
    }
    std::optional<Value> p = D::parse(v, rep);
    if (!p) {
      rep.error = "working copy " + working_path() + ": " + rep.error + "; started from the shipped 'default'";
      return rep;
    }
    working_ = std::move(*p);
    origin_ = std::string(v.get("preset").as_string("default"));
    PresetLoadReport ignore;
    std::optional<Value> oc = get_locked(origin_, ignore);
    origin_content_ = oc ? *oc : working_;
    if (!oc) rep.notes.push_back("the working copy's preset '" + origin_ + "' no longer exists");
    rep.notes.push_back("loaded the working copy (" + origin_ + (working_ == origin_content_ ? "" : " (modified)") + ")");
    ++version_;
    return rep;
  }

  // ---- the working copy ----
  Value working() const { std::lock_guard<std::mutex> lock(mu_); return working_; }
  std::string origin() const { std::lock_guard<std::mutex> lock(mu_); return origin_; }
  bool modified() const { std::lock_guard<std::mutex> lock(mu_); return !(working_ == origin_content_); }
  std::string label() const {
    std::lock_guard<std::mutex> lock(mu_);
    return working_ == origin_content_ ? origin_ : origin_ + " (modified)";
  }
  std::uint64_t version() const { std::lock_guard<std::mutex> lock(mu_); return version_; }
  std::string last_error() const { std::lock_guard<std::mutex> lock(mu_); return last_error_; }

  // The only write anyone does (rule 2): replace the working copy; `persist` false is
  // for a flag or environment value filling the run's copy without writing it.
  void set_working(Value v, bool persist = true) {
    std::lock_guard<std::mutex> lock(mu_);
    working_ = std::move(v);
    touch(persist);
  }
  // An in-place edit under the lock: fn(Value&).
  template <class Fn>
  void edit(Fn&& fn, bool persist = true) {
    std::lock_guard<std::mutex> lock(mu_);
    fn(working_);
    touch(persist);
  }

  // ---- presets ----
  std::vector<PresetInfo> list() const {
    std::vector<PresetInfo> out;
    for (std::string_view n : shipped_names()) out.push_back({std::string(n), true, ""});
    for (const std::string& n : preset_files::json_names_in(opt_.dir + "/" + std::string(D::subdir)))
      if (!is_shipped(n)) out.push_back({n, false, preset_path(n)});
    return out;
  }
  std::optional<Value> get(std::string_view name_or_path, PresetLoadReport& report) const {
    std::lock_guard<std::mutex> lock(mu_);
    return get_locked(name_or_path, report);
  }
  bool load(std::string_view name_or_path, PresetLoadReport& report, bool persist = true) {
    std::lock_guard<std::mutex> lock(mu_);
    bool partial = false;
    std::optional<Value> p = get_locked(name_or_path, report, &partial);
    if (!p) return false;
    working_ = std::move(*p);
    if (!partial) {
      origin_ = preset_files::looks_like_path(name_or_path) ? std::filesystem::path(name_or_path).stem().string() : std::string(name_or_path);
      origin_content_ = working_;
    }
    touch(persist);
    return true;
  }
  SaveResult save_as(std::string_view name, bool overwrite, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    error.clear();
    if (!preset_files::valid_preset_name(name)) { error = std::string(to_string(SaveResult::BadName)); return SaveResult::BadName; }
    std::string path;
    if (is_shipped(name)) {
      if (!opt_.may_write_shipped) { error = std::string(to_string(SaveResult::RefusedShipped)); return SaveResult::RefusedShipped; }
      path = opt_.shipped_dir + "/" + std::string(name) + ".json";
    } else {
      path = preset_path(name);
      std::error_code ec;
      if (!overwrite && std::filesystem::exists(path, ec)) { error = std::string(to_string(SaveResult::ExistsAsk)); return SaveResult::ExistsAsk; }
    }
    const std::string bytes = json::dump(D::to_json(working_, name), 2) + "\n";
    if (!preset_files::write_file_atomic(path, bytes, error)) return SaveResult::WriteFailed;
    origin_ = std::string(name);
    origin_content_ = working_;
    touch(true);
    return SaveResult::Saved;
  }

  // ---- shipped (rule 5) ----
  static std::vector<std::string_view> shipped_names() {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i < D::shipped_count(); ++i)
      if (D::shipped_at(i).first == "default") out.push_back(D::shipped_at(i).first);
    for (std::size_t i = 0; i < D::shipped_count(); ++i)
      if (D::shipped_at(i).first != "default") out.push_back(D::shipped_at(i).first);
    return out;
  }
  static bool is_shipped(std::string_view name) {
    for (std::size_t i = 0; i < D::shipped_count(); ++i)
      if (D::shipped_at(i).first == name) return true;
    return false;
  }
  static std::string_view shipped_json(std::string_view name) {
    for (std::size_t i = 0; i < D::shipped_count(); ++i)
      if (D::shipped_at(i).first == name) return D::shipped_at(i).second;
    return "";
  }
  static const Value* shipped(std::string_view name) {
    static const std::vector<std::pair<std::string, Value>> cache = [] {
      std::vector<std::pair<std::string, Value>> out;
      bool have_default = false;
      for (std::size_t i = 0; i < D::shipped_count(); ++i) {
        const auto [n, text] = D::shipped_at(i);
        std::string err;
        json::Value v = json::parse(text, err);
        PresetLoadReport rep;
        std::optional<Value> p = err.empty() ? D::parse(v, rep) : std::nullopt;
        if (!p || !rep.clean()) {
          // A shipped preset that does not load cleanly is a programming error (the
          // layout loader's standard); say so and stop rather than run half of one.
          std::fprintf(stderr, "rolltui: shipped %.*s preset '%.*s' is broken: %s\n", static_cast<int>(D::kind.size()), D::kind.data(),
                       static_cast<int>(n.size()), n.data(), err.empty() ? rep.summary().c_str() : err.c_str());
          std::abort();
        }
        have_default |= n == "default";
        out.emplace_back(std::string(n), std::move(*p));
      }
      if (!have_default) { std::fprintf(stderr, "rolltui: no shipped %.*s preset named 'default' (rule 5)\n", static_cast<int>(D::kind.size()), D::kind.data()); std::abort(); }
      return out;
    }();
    for (const auto& [n, p] : cache)
      if (n == name) return &p;
    return nullptr;
  }

  std::string working_path() const { return opt_.dir + "/" + std::string(D::working_file); }
  std::string preset_path(std::string_view name) const { return opt_.dir + "/" + std::string(D::subdir) + "/" + std::string(name) + ".json"; }
  const Options& options() const { return opt_; }

 protected:
  void touch(bool persist) {
    ++version_;
    if (persist) autosave_locked();
  }
  bool autosave_locked() {
    json::Value v = D::to_json(working_, origin_);
    v.set("preset", json::Value::string(origin_));
    std::string err;
    if (!preset_files::write_file_atomic(working_path(), json::dump(v, 2) + "\n", err)) { last_error_ = err; return false; }
    last_error_.clear();
    return true;
  }
  std::optional<Value> get_locked(std::string_view name_or_path, PresetLoadReport& report, bool* partial = nullptr) const {
    report = PresetLoadReport{};
    if (partial) *partial = false;
    if (const Value* s = shipped(name_or_path)) return *s;
    std::string path;
    if (preset_files::looks_like_path(name_or_path)) path = std::string(name_or_path);
    else path = preset_path(name_or_path);
    std::string text;
    if (!preset_files::read_file(path, text)) {
      report.error = "no " + std::string(D::kind) + " preset '" + std::string(name_or_path) + "' (not shipped, and " + path + " is not readable)";
      return std::nullopt;
    }
    std::string err;
    json::Value v = json::parse(text, err);
    if (!err.empty()) { report.error = path + ": " + err; return std::nullopt; }
    if (std::optional<Value> p = D::parse_partial(v, working_, report)) {  // parse_partial notes what it kept
      if (partial) *partial = true;
      for (std::string& n : report.notes) n = path + " " + n;
      return p;
    }
    if (!report.error.empty()) { report.error = path + ": " + report.error; return std::nullopt; }
    std::optional<Value> p = D::parse(v, report);
    if (!p) report.error = path + ": " + report.error;
    return p;
  }

  Options opt_;
  mutable std::mutex mu_;
  Value working_;
  std::string origin_ = "default";
  Value origin_content_;
  std::uint64_t version_ = 1;
  std::string last_error_;
};

}  // namespace rolltui
