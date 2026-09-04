// rolltui/PresetsCpp.cpp — the C++ side of the preset mechanics, behind the same boundary
// as `c/rolltui_presets.c` (Phase 15 m3). One of the two links; the flag `-DROLLTUI_C`
// picks which. See rolltui_presets.h for the boundary's rules and rolltui/Presets.hpp for
// the five rules themselves.
//
// This is the shape the module has always had — `std::string` for a path, `std::filesystem`
// for the directory work, `std::mutex` for the lock — kept deliberately, so the two
// implementations differ in the way the languages do and not because one of them was
// rewritten while it was being moved. What it does NOT keep is the template: both sides of
// this boundary take the domain as a table of function pointers, because that is the shape
// the C side forces and a measurement of two different shapes would measure nothing.
#include "rolltui/c/rolltui_presets.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

// The one sink everything here builds a string in, so a `put` callback and a
// `std::string` meet in exactly one place.
void put_string(void* ctx, const char* s, std::size_t len) { static_cast<std::string*>(ctx)->append(s, len); }

}  // namespace

extern "C" {

int rolltui_preset_read_file(const char* path, std::size_t path_len, RolltuiPutFn put, void* ctx) {
  std::ifstream in(std::string(path, path_len), std::ios::binary);
  if (!in) return 0;
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string s = ss.str();
  put(ctx, s.data(), s.size());
  return 1;
}

int rolltui_preset_write_file_atomic(const char* path_p, std::size_t path_len, const char* bytes, std::size_t len,
                                     RolltuiPutFn err, void* err_ctx) {
  const std::string path(path_p, path_len);
  auto fail = [&](const std::string& why) {
    err(err_ctx, why.data(), why.size());
    return 0;
  };
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  if (ec) return fail("cannot create " + fs::path(path).parent_path().string() + ": " + ec.message());
  // Write to a sibling temp file, then rename — a reader sees the old complete file or the
  // new complete file, never a mix (the state-file rule from ResilientModelManager).
  const std::string tmp = path + ".tmp." + std::to_string(::getpid());
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return fail("cannot write " + tmp + ": " + std::strerror(errno));
    out.write(bytes, static_cast<std::streamsize>(len));
    if (!out) return fail("short write to " + tmp);
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    const std::string why = "cannot rename " + tmp + " to " + path + ": " + std::strerror(errno);
    std::remove(tmp.c_str());
    return fail(why);
  }
  return 1;
}

}  // extern "C"

namespace {

std::vector<std::string> json_names(std::string_view dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const fs::directory_entry& e : fs::directory_iterator(std::string(dir), ec)) {
    if (!e.is_regular_file(ec)) continue;
    const fs::path p = e.path();
    if (p.extension() != ".json") continue;
    if (!p.stem().string().empty() && p.stem().string()[0] == '.') continue;
    out.push_back(p.stem().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

extern "C" {

void rolltui_preset_json_names_in(const char* dir, std::size_t dir_len, RolltuiPutFn put, void* ctx) {
  for (const std::string& n : json_names(std::string_view(dir, dir_len))) put(ctx, n.data(), n.size());
}

int rolltui_preset_looks_like_path(const char* s_p, std::size_t len) {
  const std::string_view s(s_p, len);
  return (s.find('/') != std::string_view::npos || (s.size() > 5 && s.substr(s.size() - 5) == ".json")) ? 1 : 0;
}

int rolltui_preset_valid_name(const char* name_p, std::size_t len) {
  const std::string_view name(name_p, len);
  if (name.empty() || name.size() > 64 || name[0] == '.') return 0;
  for (char c : name)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return 0;
  return 1;
}

}  // extern "C"

// ---- the shipped cache ----------------------------------------------------------------
// OWNED by the domain descriptor, which is a process-wide static on the other side; that is
// what `rolltui_preset_domain_release` hands back at `rolltui::shutdown()`.
struct RolltuiPresetShippedCache {
  std::vector<std::pair<std::string, void*>> entries;
  RolltuiPresetDomain* owner = nullptr;
};

namespace {

std::pair<std::string_view, std::string_view> shipped_row(RolltuiPresetDomain* d, std::size_t i) {
  const char* name = nullptr;
  const char* text = nullptr;
  std::size_t nlen = 0, tlen = 0;
  d->shipped_at(i, &name, &nlen, &text, &tlen);
  return {std::string_view(name, nlen), std::string_view(text, tlen)};
}

void ensure_cache(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep, void* scratch) {
  if (d->cache) return;
  std::unique_ptr<RolltuiPresetShippedCache> c = std::make_unique<RolltuiPresetShippedCache>();
  c->owner = d;
  bool have_default = false;
  for (std::size_t i = 0; i < d->shipped_count(); ++i) {
    const auto [name, text] = shipped_row(d, i);
    rep->reset(scratch);
    void* v = d->parse(text.data(), text.size(), scratch);
    if (!v) {
      // A shipped preset that does not load cleanly is a programming error (the layout
      // loader's standard); say so and stop rather than run half of one.
      std::string why;
      rep->get_error(scratch, put_string, &why);
      std::fprintf(stderr, "rolltui: shipped %.*s preset '%.*s' is broken: %s\n", static_cast<int>(d->kind_len),
                   d->kind, static_cast<int>(name.size()), name.data(), why.c_str());
      std::abort();
    }
    have_default |= name == "default";
    c->entries.emplace_back(std::string(name), v);
  }
  if (!have_default) {
    std::fprintf(stderr, "rolltui: no shipped %.*s preset named 'default' (rule 5)\n", static_cast<int>(d->kind_len),
                 d->kind);
    std::abort();
  }
  d->cache = c.release();
}

}  // namespace

extern "C" {

const void* rolltui_preset_shipped(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep_fns,
                                   void* scratch_report, const char* name, std::size_t len) {
  ensure_cache(d, rep_fns, scratch_report);
  const std::string_view want(name, len);
  for (const auto& [n, v] : d->cache->entries)
    if (n == want) return v;
  return nullptr;
}

int rolltui_preset_is_shipped(RolltuiPresetDomain* d, const char* name, std::size_t len) {
  // Asked WITHOUT parsing anything: the names are in the embedded table.
  const std::string_view want(name, len);
  for (std::size_t i = 0; i < d->shipped_count(); ++i)
    if (shipped_row(d, i).first == want) return 1;
  return 0;
}

void rolltui_preset_shipped_names(RolltuiPresetDomain* d, RolltuiPutFn put, void* ctx) {
  for (int pass = 0; pass < 2; ++pass)
    for (std::size_t i = 0; i < d->shipped_count(); ++i) {
      const std::string_view n = shipped_row(d, i).first;
      if ((pass == 0) != (n == "default")) continue;
      put(ctx, n.data(), n.size());
    }
}

void rolltui_preset_domain_release(RolltuiPresetDomain* d) {
  if (!d || !d->cache) return;
  const std::unique_ptr<RolltuiPresetShippedCache> owned(d->cache);
  for (auto& [n, v] : owned->entries) d->destroy(v);
  d->cache = nullptr;
}

}  // extern "C"

// ---- the store --------------------------------------------------------------------------
struct RolltuiPresetStore {
  RolltuiPresetDomain* d = nullptr;
  const RolltuiPresetReportFns* rep = nullptr;
  std::string dir, shipped_dir;
  bool may_write_shipped = false;

  mutable std::mutex mu;
  void* working = nullptr;
  void* origin_content = nullptr;
  std::string origin = "default";
  std::string last_error;
  std::uint64_t version = 1;

  std::string working_path() const { return dir + "/" + std::string(d->working_file, d->working_file_len); }
  std::string preset_path(std::string_view name) const {
    return dir + "/" + std::string(d->subdir, d->subdir_len) + "/" + std::string(name) + ".json";
  }
  std::string to_json(const void* v, std::string_view name, bool with_origin) const {
    std::string out;
    (with_origin ? d->to_json_with_origin : d->to_json)(v, name.data(), name.size(), put_string, &out);
    return out;
  }
  bool autosave_locked() {
    const std::string bytes = to_json(working, origin, true);
    std::string err;
    if (!rolltui_preset_write_file_atomic(working_path().data(), working_path().size(), bytes.data(), bytes.size(),
                                          put_string, &err)) {
      last_error = err;
      return false;
    }
    last_error.clear();
    return true;
  }
  void touch_locked(bool persist) {
    ++version;
    if (persist) autosave_locked();
  }
  // The one read that `get`, `load` and `start` all go through.
  void* get_locked(std::string_view name, void* report, bool* partial) const {
    rep->reset(report);
    if (partial) *partial = false;
    if (const void* s = rolltui_preset_shipped(d, rep, report, name.data(), name.size())) {
      rep->reset(report);
      return d->clone(s);
    }
    const std::string path = rolltui_preset_looks_like_path(name.data(), name.size()) ? std::string(name)
                                                                                     : preset_path(name);
    std::string text;
    if (!rolltui_preset_read_file(path.data(), path.size(), put_string, &text)) {
      const std::string why = "no " + std::string(d->kind, d->kind_len) + " preset '" + std::string(name) +
                              "' (not shipped, and " + path + " is not readable)";
      rep->set_error(report, why.data(), why.size());
      return nullptr;
    }
    // PARTIAL FIRST: a colours-only theme file fills part of the working copy and says what
    // it kept (Presets.hpp). Its notes get the path in front of them.
    if (void* p = d->parse_partial(text.data(), text.size(), working, report)) {
      if (partial) *partial = true;
      const std::string prefix = path + " ";
      rep->prefix_notes(report, prefix.data(), prefix.size());
      return p;
    }
    std::string err;
    rep->get_error(report, put_string, &err);
    if (!err.empty()) {
      const std::string why = path + ": " + err;
      rep->set_error(report, why.data(), why.size());
      return nullptr;
    }
    void* p = d->parse(text.data(), text.size(), report);
    if (!p) {
      err.clear();
      rep->get_error(report, put_string, &err);
      const std::string why = path + ": " + err;
      rep->set_error(report, why.data(), why.size());
    }
    return p;
  }
};

extern "C" {

RolltuiPresetStore* rolltui_preset_store_new(RolltuiPresetDomain* d, const RolltuiPresetReportFns* rep_fns,
                                             const char* dir, std::size_t dir_len, int may_write_shipped,
                                             const char* shipped_dir, std::size_t shipped_dir_len,
                                             void* scratch_report) {
  std::unique_ptr<RolltuiPresetStore> s = std::make_unique<RolltuiPresetStore>();
  s->d = d;
  s->rep = rep_fns;
  s->dir.assign(dir, dir_len);
  s->shipped_dir.assign(shipped_dir, shipped_dir_len);
  s->may_write_shipped = may_write_shipped != 0;
  const void* def = rolltui_preset_shipped(d, rep_fns, scratch_report, "default", 7);
  s->working = d->clone(def);
  s->origin_content = d->clone(def);
  return s.release();
}

void rolltui_preset_store_free(RolltuiPresetStore* s) {
  const std::unique_ptr<RolltuiPresetStore> owned(s);  // takes it back, and frees it on the way out
  if (!s) return;
  if (s->working) s->d->destroy(s->working);
  if (s->origin_content) s->d->destroy(s->origin_content);
}

void rolltui_preset_store_start(RolltuiPresetStore* s, void* report, void* scratch_report) {
  const std::lock_guard<std::mutex> lock(s->mu);
  s->rep->reset(report);
  const std::string path = s->working_path();
  std::string text;
  auto note = [&](const std::string& n) { s->rep->add_note(report, n.data(), n.size()); };
  if (!rolltui_preset_read_file(path.data(), path.size(), put_string, &text)) {
    note("no working copy at " + path + "; started from the shipped 'default'");
    return;
  }
  void* parsed = s->d->parse(text.data(), text.size(), report);
  if (!parsed) {
    std::string why;
    s->rep->get_error(report, put_string, &why);
    const std::string msg = "working copy " + path + ": " + (why.empty() ? "unreadable" : why) +
                            "; started from the shipped 'default'";
    s->rep->set_error(report, msg.data(), msg.size());
    return;
  }
  s->d->destroy(s->working);
  s->working = parsed;
  // WHICH PRESET IT CAME FROM: the domain wrote it as "preset" and reads it back, because
  // the JSON never crosses this boundary (rolltui_presets.h).
  s->origin.clear();
  s->d->origin_of(text.data(), text.size(), put_string, &s->origin);
  if (s->origin.empty()) s->origin = "default";
  void* oc = s->get_locked(s->origin, scratch_report, nullptr);
  s->d->destroy(s->origin_content);
  s->origin_content = oc ? oc : s->d->clone(s->working);
  if (!oc) note("the working copy's preset '" + s->origin + "' no longer exists");
  note("loaded the working copy (" + s->origin + (s->d->equal(s->working, s->origin_content) ? "" : " (modified)") + ")");
  ++s->version;
}

void* rolltui_preset_store_working(const RolltuiPresetStore* s) {
  const std::lock_guard<std::mutex> lock(s->mu);
  return s->d->clone(s->working);
}

const char* rolltui_preset_store_origin(const RolltuiPresetStore* s, std::size_t* len) {
  *len = s->origin.size();
  return s->origin.data();
}

const char* rolltui_preset_store_last_error(const RolltuiPresetStore* s, std::size_t* len) {
  *len = s->last_error.size();
  return s->last_error.data();
}

int rolltui_preset_store_modified(const RolltuiPresetStore* s) {
  const std::lock_guard<std::mutex> lock(s->mu);
  return s->d->equal(s->working, s->origin_content) ? 0 : 1;
}

unsigned long long rolltui_preset_store_version(const RolltuiPresetStore* s) {
  const std::lock_guard<std::mutex> lock(s->mu);
  return s->version;
}

void rolltui_preset_store_set_working(RolltuiPresetStore* s, void* v, int persist) {
  const std::lock_guard<std::mutex> lock(s->mu);
  s->d->destroy(s->working);
  s->working = v;
  s->touch_locked(persist != 0);
}

void rolltui_preset_store_edit(RolltuiPresetStore* s, void (*fn)(void* value, void* ctx), void* ctx, int persist) {
  const std::lock_guard<std::mutex> lock(s->mu);
  fn(s->working, ctx);
  s->touch_locked(persist != 0);
}

void rolltui_preset_store_list(const RolltuiPresetStore* s, RolltuiPresetInfoFn put, void* ctx) {
  // The shipped ones first, "default" ahead of the rest — the order a chooser offers them
  // in, and the same order `shipped_names()` gives.
  for (int pass = 0; pass < 2; ++pass)
    for (std::size_t i = 0; i < s->d->shipped_count(); ++i) {
      const std::string_view n = shipped_row(s->d, i).first;
      if ((pass == 0) != (n == "default")) continue;
      put(ctx, n.data(), n.size(), 1, "", 0);
    }
  for (const std::string& n : json_names(s->dir + "/" + std::string(s->d->subdir, s->d->subdir_len))) {
    // A user file that shadows a shipped name is never listed as a user preset.
    if (rolltui_preset_is_shipped(s->d, n.data(), n.size())) continue;
    const std::string path = s->preset_path(n);
    put(ctx, n.data(), n.size(), 0, path.data(), path.size());
  }
}

void* rolltui_preset_store_get(const RolltuiPresetStore* s, const char* name, std::size_t len, void* report) {
  const std::lock_guard<std::mutex> lock(s->mu);
  return s->get_locked(std::string_view(name, len), report, nullptr);
}

int rolltui_preset_store_load(RolltuiPresetStore* s, const char* name_p, std::size_t len, void* report, int persist) {
  const std::lock_guard<std::mutex> lock(s->mu);
  const std::string_view name(name_p, len);
  bool partial = false;
  void* v = s->get_locked(name, report, &partial);
  if (!v) return 0;
  s->d->destroy(s->working);
  s->working = v;
  if (!partial) {
    s->origin = rolltui_preset_looks_like_path(name_p, len) ? fs::path(name).stem().string() : std::string(name);
    s->d->destroy(s->origin_content);
    s->origin_content = s->d->clone(s->working);
  }
  s->touch_locked(persist != 0);
  return 1;
}

int rolltui_preset_store_save_as(RolltuiPresetStore* s, const char* name_p, std::size_t len, int overwrite,
                                 RolltuiPutFn err, void* err_ctx) {
  const std::lock_guard<std::mutex> lock(s->mu);
  const std::string_view name(name_p, len);
  if (!rolltui_preset_valid_name(name_p, len)) return ROLLTUI_SAVE_BAD_NAME;
  std::string path;
  if (rolltui_preset_is_shipped(s->d, name_p, len)) {
    if (!s->may_write_shipped) return ROLLTUI_SAVE_REFUSED_SHIPPED;
    path = s->shipped_dir + "/" + std::string(name) + ".json";
  } else {
    path = s->preset_path(name);
    std::error_code ec;
    if (!overwrite && fs::exists(path, ec)) return ROLLTUI_SAVE_EXISTS_ASK;
  }
  const std::string bytes = s->to_json(s->working, name, false);
  if (!rolltui_preset_write_file_atomic(path.data(), path.size(), bytes.data(), bytes.size(), err, err_ctx))
    return ROLLTUI_SAVE_WRITE_FAILED;
  s->origin = std::string(name);
  s->d->destroy(s->origin_content);
  s->origin_content = s->d->clone(s->working);
  s->touch_locked(true);
  return ROLLTUI_SAVE_SAVED;
}

void rolltui_preset_store_working_path(const RolltuiPresetStore* s, RolltuiPutFn put, void* ctx) {
  const std::string p = s->working_path();
  put(ctx, p.data(), p.size());
}

void rolltui_preset_store_preset_path(const RolltuiPresetStore* s, const char* name, std::size_t len,
                                      RolltuiPutFn put, void* ctx) {
  const std::string p = s->preset_path(std::string_view(name, len));
  put(ctx, p.data(), p.size());
}

}  // extern "C"
