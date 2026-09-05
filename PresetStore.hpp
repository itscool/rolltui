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
// PHASE 15 m3 — THE MECHANICS ARE BEHIND A C BOUNDARY (`rolltui/c/rolltui_presets.h`).
// This template is now the ADAPTER: it
// turns a Domain traits type into the descriptor of function pointers that boundary takes,
// and turns the boundary's answers back into the shapes a host already writes against.
//
// **THIS IS THE ONE PLACE THE LIBRARY USES A TEMPLATE FOR REAL, so it is where "what does C
// cost for a generic container" gets its only honest answer.** The C header states the
// trade row by row; the short version is that everything the template got for free —
// `Value working_`, `working_ == origin_content_`, `D::parse(...)` — becomes a `void*` and a
// function pointer, and the REPORT costs five more of them because the mechanics write
// English into a type they cannot name. The one thing that got BETTER is that `json::Value`
// stops crossing at all: `parse` takes TEXT, so the store never sees a tree it has no use
// for, and a module that has not ported stays entirely on this side.
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

#include <memory>

#include "rolltui/Json.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Lifetime.hpp"
#include "rolltui/c/rolltui_presets.h"
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

namespace detail {

// ---- the report, as the five things the mechanics do to one -----------------------------
inline void report_reset(void* r) { *static_cast<PresetLoadReport*>(r) = PresetLoadReport{}; }
inline void report_set_error(void* r, const char* s, std::size_t len) {
  static_cast<PresetLoadReport*>(r)->error.assign(s, len);
}
inline void report_get_error(const void* r, RolltuiPutFn put, void* ctx) {
  const std::string& e = static_cast<const PresetLoadReport*>(r)->error;
  put(ctx, e.data(), e.size());
}
inline void report_add_note(void* r, const char* s, std::size_t len) {
  static_cast<PresetLoadReport*>(r)->notes.emplace_back(s, len);
}
inline void report_prefix_notes(void* r, const char* p, std::size_t len) {
  const std::string prefix(p, len);
  for (std::string& n : static_cast<PresetLoadReport*>(r)->notes) n = prefix + n;
}
inline const RolltuiPresetReportFns& report_fns() {
  static const RolltuiPresetReportFns f = {report_reset, report_set_error, report_get_error, report_add_note,
                                           report_prefix_notes};
  return f;
}

// The one place a `std::string` and a `put` callback meet.
inline void put_string(void* ctx, const char* s, std::size_t len) { static_cast<std::string*>(ctx)->append(s, len); }

// ---- one Domain, as the descriptor the boundary takes -------------------------------------
// Everything below is a captureless lambda, so each is a plain function pointer and the
// descriptor costs no closure. A VALUE crossing as a `void*` is OWNED by whoever holds it,
// and `destroy` is the only thing that frees one — there is no hand-rolled `delete` on this
// path, which the ownership test checks for.
template <class D>
RolltuiPresetDomain& domain_storage() {
  static RolltuiPresetDomain d = [] {
    using Value = typename D::Value;
    RolltuiPresetDomain x{};
    x.kind = D::kind.data();
    x.kind_len = D::kind.size();
    x.working_file = D::working_file.data();
    x.working_file_len = D::working_file.size();
    x.subdir = D::subdir.data();
    x.subdir_len = D::subdir.size();
    x.shipped_count = [] { return D::shipped_count(); };
    x.shipped_at = [](std::size_t i, const char** name, std::size_t* nlen, const char** text, std::size_t* tlen) {
      const auto [n, t] = D::shipped_at(i);
      *name = n.data();
      *nlen = n.size();
      *text = t.data();
      *tlen = t.size();
    };
    x.parse = [](const char* t, std::size_t n, void* rep) -> void* {
      PresetLoadReport& r = *static_cast<PresetLoadReport*>(rep);
      std::string err;
      json::Value v = json::parse(std::string_view(t, n), err);
      // "unreadable" is said HERE rather than by the store, because only this side knows
      // the JSON was the thing that failed — the store's own wrapper adds the path.
      if (!err.empty()) { r.error = "unreadable (" + err + ")"; return nullptr; }
      std::optional<Value> p = D::parse(v, r);
      return p ? std::make_unique<Value>(std::move(*p)).release() : nullptr;
    };
    x.parse_partial = [](const char* t, std::size_t n, const void* working, void* rep) -> void* {
      PresetLoadReport& r = *static_cast<PresetLoadReport*>(rep);
      std::string err;
      json::Value v = json::parse(std::string_view(t, n), err);
      if (!err.empty()) { r.error = "unreadable (" + err + ")"; return nullptr; }
      std::optional<Value> p = D::parse_partial(v, *static_cast<const Value*>(working), r);
      return p ? std::make_unique<Value>(std::move(*p)).release() : nullptr;
    };
    x.to_json = [](const void* v, const char* name, std::size_t len, RolltuiPutFn put, void* ctx) {
      const std::string s = json::dump(D::to_json(*static_cast<const Value*>(v), std::string_view(name, len)), 2) + "\n";
      put(ctx, s.data(), s.size());
    };
    x.to_json_with_origin = [](const void* v, const char* name, std::size_t len, RolltuiPutFn put, void* ctx) {
      json::Value j = D::to_json(*static_cast<const Value*>(v), std::string_view(name, len));
      j.set("preset", json::Value::string(std::string(name, len)));
      const std::string s = json::dump(j, 2) + "\n";
      put(ctx, s.data(), s.size());
    };
    x.origin_of = [](const char* t, std::size_t n, RolltuiPutFn put, void* ctx) {
      std::string err;
      const json::Value v = json::parse(std::string_view(t, n), err);
      const std::string_view o = err.empty() ? v.get("preset").as_string("default") : "default";
      put(ctx, o.data(), o.size());
    };
    x.clone = [](const void* v) -> void* { return std::make_unique<Value>(*static_cast<const Value*>(v)).release(); };
    x.destroy = [](void* v) { const std::unique_ptr<Value> owned(static_cast<Value*>(v)); };
    x.equal = [](const void* a, const void* b) {
      return *static_cast<const Value*>(a) == *static_cast<const Value*>(b) ? 1 : 0;
    };
    return x;
  }();
  return d;
}

// THE DESCRIPTOR, WITH ITS RELEASER RE-REGISTERED ON EVERY REBUILD. A domain's parsed
// shipped presets are a process-wide retainer, and `shutdown()` clears its own registry as
// it runs — so a `static bool once` would release them the first time and never again. The
// rule is `ThreadHandle`'s, applied one level up: register where the thing is MADE, and the
// cache being empty is exactly when it is about to be.
template <class D>
RolltuiPresetDomain& domain_for() {
  RolltuiPresetDomain& d = domain_storage<D>();
  if (!d.cache) on_shutdown([] { rolltui_preset_domain_release(&domain_storage<D>()); });
  return d;
}

}  // namespace detail

template <class D>
class PresetStore {
 public:
  using Value = typename D::Value;
  struct Options {
    std::string dir;                 // the host's rolltui directory
    bool may_write_shipped = false;  // the editor's privilege (rule 5)
    std::string shipped_dir;         // where shipped presets are written when allowed
  };
  // OWNED, through a `unique_ptr` with a deleter that calls the C free — the same shape
  // `Frame`, `KeyDecoder`, `EffectMap` and `Bindings` use.
  struct Handle {
    void operator()(RolltuiPresetStore* p) const { rolltui_preset_store_free(p); }
  };

  explicit PresetStore(Options o) : opt_(std::move(o)) {
    PresetLoadReport scratch;
    s_.reset(rolltui_preset_store_new(&detail::domain_for<D>(), &detail::report_fns(), opt_.dir.data(),
                                      opt_.dir.size(), opt_.may_write_shipped, opt_.shipped_dir.data(),
                                      opt_.shipped_dir.size(), &scratch));
  }

  // Startup: the autosaved working copy when present and loadable, else "default".
  PresetLoadReport start() {
    PresetLoadReport rep, scratch;
    rolltui_preset_store_start(s_.get(), &rep, &scratch);
    return rep;
  }

  // ---- the working copy ----
  Value working() const { return take(rolltui_preset_store_working(s_.get())); }
  std::string origin() const {
    std::size_t len = 0;
    const char* p = rolltui_preset_store_origin(s_.get(), &len);
    return std::string(p, len);
  }
  bool modified() const { return rolltui_preset_store_modified(s_.get()) != 0; }
  std::string label() const { return modified() ? origin() + " (modified)" : origin(); }
  std::uint64_t version() const { return rolltui_preset_store_version(s_.get()); }
  std::string last_error() const {
    std::size_t len = 0;
    const char* p = rolltui_preset_store_last_error(s_.get(), &len);
    return std::string(p, len);
  }

  // The only write anyone does (rule 2): replace the working copy; `persist` false is
  // for a flag or environment value filling the run's copy without writing it.
  void set_working(Value v, bool persist = true) {
    rolltui_preset_store_set_working(s_.get(), std::make_unique<Value>(std::move(v)).release(), persist);
  }
  // An in-place edit under the lock: fn(Value&). The callback crosses as a function pointer
  // and a context, which is what a `void*` boundary has instead of a closure.
  template <class Fn>
  void edit(Fn&& fn, bool persist = true) {
    Fn* f = &fn;
    rolltui_preset_store_edit(
        s_.get(), [](void* value, void* ctx) { (*static_cast<Fn*>(ctx))(*static_cast<Value*>(value)); }, f, persist);
  }

  // ---- presets ----
  std::vector<PresetInfo> list() const {
    std::vector<PresetInfo> out;
    rolltui_preset_store_list(
        s_.get(),
        [](void* ctx, const char* name, std::size_t nlen, int shipped, const char* path, std::size_t plen) {
          static_cast<std::vector<PresetInfo>*>(ctx)->push_back(
              {std::string(name, nlen), shipped != 0, std::string(path, plen)});
        },
        &out);
    return out;
  }
  std::optional<Value> get(std::string_view name_or_path, PresetLoadReport& report) const {
    void* v = rolltui_preset_store_get(s_.get(), name_or_path.data(), name_or_path.size(), &report);
    if (!v) return std::nullopt;
    return take(v);
  }
  bool load(std::string_view name_or_path, PresetLoadReport& report, bool persist = true) {
    return rolltui_preset_store_load(s_.get(), name_or_path.data(), name_or_path.size(), &report, persist) != 0;
  }
  SaveResult save_as(std::string_view name, bool overwrite, std::string& error) {
    error.clear();
    const int code = rolltui_preset_store_save_as(s_.get(), name.data(), name.size(), overwrite, detail::put_string,
                                                  &error);
    const SaveResult r = static_cast<SaveResult>(code);
    // Every outcome but WriteFailed has a fixed sentence; only that one has a reason of its
    // own, which the boundary appended to `error` on its way out.
    if (r != SaveResult::Saved && r != SaveResult::WriteFailed) error = std::string(to_string(r));
    return r;
  }

  // ---- shipped (rule 5) ----
  static std::vector<std::string_view> shipped_names() {
    std::vector<std::string_view> out;
    rolltui_preset_shipped_names(&detail::domain_for<D>(),
                                 [](void* ctx, const char* n, std::size_t len) {
                                   static_cast<std::vector<std::string_view>*>(ctx)->emplace_back(n, len);
                                 },
                                 &out);
    return out;
  }
  static bool is_shipped(std::string_view name) {
    return rolltui_preset_is_shipped(&detail::domain_for<D>(), name.data(), name.size()) != 0;
  }
  static std::string_view shipped_json(std::string_view name) {
    for (std::size_t i = 0; i < D::shipped_count(); ++i)
      if (D::shipped_at(i).first == name) return D::shipped_at(i).second;
    return "";
  }
  // A BORROW of the domain's parsed cache, valid until `shutdown()`. nullptr when the name
  // is not shipped.
  static const Value* shipped(std::string_view name) {
    PresetLoadReport scratch;
    return static_cast<const Value*>(rolltui_preset_shipped(&detail::domain_for<D>(), &detail::report_fns(), &scratch,
                                                            name.data(), name.size()));
  }

  std::string working_path() const {
    std::string out;
    rolltui_preset_store_working_path(s_.get(), detail::put_string, &out);
    return out;
  }
  std::string preset_path(std::string_view name) const {
    std::string out;
    rolltui_preset_store_preset_path(s_.get(), name.data(), name.size(), detail::put_string, &out);
    return out;
  }
  const Options& options() const { return opt_; }

 protected:
  // A value the boundary handed over is OURS: taken back into a `unique_ptr` and copied out,
  // which is what `working()` returning by value already meant and the only place a
  // `void*` becomes a `Value` again.
  static Value take(void* v) {
    const std::unique_ptr<Value> owned(static_cast<Value*>(v));
    return *owned;
  }

  Options opt_;
  std::unique_ptr<RolltuiPresetStore, Handle> s_;
};

}  // namespace rolltui
