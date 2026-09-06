//
// presets_test.cpp — the preset system (milestone 11 / 11e, Phase 10 m1) on all three
// domains: the five rules in Presets.hpp, the shipped files against the built-in themes
// and layouts, the four-rung precedence table (every combination of present/absent
// rungs), the file format's report, and the OSC 11 reply parser with the light/dark
// rule. Phase 10 m1 adds the Layout domain.
// Runs in a scratch directory under $TMPDIR it creates and removes.
//
// PHASE 17 m2c: this file calls the C directly (`rolltui/c/rolltui_presets.h`) instead of
// `rolltui::PresetStore<Domain>` (Presets.hpp/PresetStore.hpp, deleted from this file's
// dependencies). `PresetStore<D>` was only ever the ADAPTER that turns a Domain traits type
// into the descriptor of function pointers `rolltui_preset_store_new` takes.
// PHASE 18 m3: the three descriptors are the LIBRARY's (`rolltui_preset_domain`). This file had
// assembled them itself from `rolltui_theme_preset_domain_init` and its siblings — as had roll,
// the studio, `lifetime_test` and the C consumer, five spellings of one assembly — and never
// released their cache. The library's Bindings domain carries `rolltui_undeliverable_reason_fn`
// where this file passed NULL; this suite's bindings fixtures never touch an undeliverable
// chord, so that changes nothing this file asserts.
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

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
void put_str(void* ctx, const char* s, std::size_t len) { static_cast<std::string*>(ctx)->append(s, len); }
std::string str_of(const RolltuiStr& s) { return std::string(s.p ? s.p : "", s.n); }

// ---- the three domain descriptors are `rolltui_preset_domain(...)` throughout (see the header
// note); the NULL-`parse_partial` guard this file once worked around locally is the library's
// (`get_locked`, rolltui_presets.c) and is exercised by the user-saved layout/bindings loads below.

// ---- the three domain-specific report shapes, as RAII over the transparent C structs --------
// Each `_release` is the library's own; the shape itself (which fields exist) is
// `rolltui_presets.h`'s, not a copy — only the destructor/`clean()`/`summary()` sugar is added,
// the same "small fixture may hold a SHAPE, never a rule or a word" allowance the C++ template
// adapter used to give for free.

struct ThemePresetReport : RolltuiThemePresetReport {
  ThemePresetReport() : RolltuiThemePresetReport{} {}
  ThemePresetReport(const ThemePresetReport&) = delete;
  ~ThemePresetReport() { rolltui_theme_preset_report_release(this); }
  bool clean() const {
    return error.empty() && bad_values_n == 0 && unknown_keys_n == 0 && colours.error.empty() &&
           colours.missing_roles_n == 0 && colours.unknown_keys_n == 0 && colours.bad_values_n == 0;
  }
  std::string summary() const {
    RolltuiStr out{};
    rolltui_preset_report_summary(&error, bad_values, bad_values_n, unknown_keys, unknown_keys_n, &colours, nullptr,
                                  nullptr, &out);
    std::string s = str_of(out);
    rolltui_str_free(&out);
    return s;
  }
};
struct LayoutPresetReport : RolltuiLayoutPresetReport {
  LayoutPresetReport() : RolltuiLayoutPresetReport{} {}
  LayoutPresetReport(const LayoutPresetReport&) = delete;
  ~LayoutPresetReport() { rolltui_layout_preset_report_release(this); }
  bool clean() const { return error.empty() && rolltui_layout_report_clean(&layout) != 0; }
  std::string summary() const {
    RolltuiStr out{};
    rolltui_preset_report_summary(&error, nullptr, 0, nullptr, 0, nullptr, &layout, nullptr, &out);
    std::string s = str_of(out);
    rolltui_str_free(&out);
    return s;
  }
};
struct BindingsPresetReport : RolltuiBindingsPresetReport {
  BindingsPresetReport() : RolltuiBindingsPresetReport{} {}
  BindingsPresetReport(const BindingsPresetReport&) = delete;
  ~BindingsPresetReport() { rolltui_bindings_preset_report_release(this); }
  bool clean() const { return error.empty() && unknown_keys_n == 0 && rolltui_bindings_report_clean(&bindings) != 0; }
  std::string summary() const {
    RolltuiStr out{};
    rolltui_preset_report_summary(&error, nullptr, 0, unknown_keys, unknown_keys_n, nullptr, nullptr, &bindings, &out);
    std::string s = str_of(out);
    rolltui_str_free(&out);
    return s;
  }
};

// `rolltui::default_bindings_json()`'s own port: the embedded table, scanned directly —
// independent of the Bindings domain's own `shipped_at` walk, so the two-code-paths check
// below is not comparing a value to itself.
std::string default_bindings_json_c() {
  for (std::size_t i = 0; i < rolltui_kBindingsPresetCount; ++i)
    if (std::string_view(rolltui_kBindingsPresets[i].name) == "default") return rolltui_kBindingsPresets[i].text;
  return "";
}

// ---- the store: common mechanics shared by all three domains --------------------------------
class StoreBase {
 public:
  explicit StoreBase(RolltuiPresetStore* s) : s_(s) {}
  StoreBase(const StoreBase&) = delete;
  ~StoreBase() { rolltui_preset_store_free(s_); }

  std::string origin() const {
    std::size_t n = 0;
    const char* p = rolltui_preset_store_origin(s_, &n);
    return std::string(p, n);
  }
  bool modified() const { return rolltui_preset_store_modified(s_) != 0; }
  std::string label() const {
    RolltuiStr out{};
    rolltui_preset_store_label(s_, &out);
    std::string v = str_of(out);
    rolltui_str_free(&out);
    return v;
  }
  std::uint64_t version() const { return rolltui_preset_store_version(s_); }
  std::string last_error() const {
    std::size_t n = 0;
    const char* p = rolltui_preset_store_last_error(s_, &n);
    return std::string(p, n);
  }
  std::string working_path() const {
    RolltuiStr out;
    rolltui_preset_store_working_path(s_, &out);
    return out.str();
  }
  std::string preset_path(std::string_view name) const {
    RolltuiStr out;
    rolltui_preset_store_preset_path(s_, name.data(), name.size(), &out);
    return out.str();
  }
  void list(RolltuiPresetList& out) const { rolltui_preset_store_list(s_, &out); }
  int save_as(std::string_view name, bool overwrite, RolltuiStr& error) {
    return rolltui_preset_store_save_as(s_, name.data(), name.size(), overwrite ? 1 : 0, &error);
  }
  std::string working_value(std::string_view key) const {
    RolltuiStr out{};
    rolltui_preset_working_value(s_, key.data(), key.size(), &out);
    std::string v = str_of(out);
    rolltui_str_free(&out);
    return v;
  }
  RolltuiPresetStore* handle() const { return s_; }

 protected:
  RolltuiPresetStore* s_;
};

// ---- the Theme store -------------------------------------------------------------------------
struct ThemeValueHandle {
  const RolltuiPresetStore* s;
  RolltuiThemePresetValue* v;
  ThemeValueHandle(const RolltuiPresetStore* store, void* p) : s(store), v(static_cast<RolltuiThemePresetValue*>(p)) {}
  ThemeValueHandle(const ThemeValueHandle&) = delete;
  ThemeValueHandle(ThemeValueHandle&& o) noexcept : s(o.s), v(o.v) { o.v = nullptr; }
  ~ThemeValueHandle() { rolltui_preset_store_value_free(s, v); }
  const RolltuiThemePresetValue* operator->() const { return v; }
  const RolltuiThemePresetValue& operator*() const { return *v; }
  explicit operator bool() const { return v != nullptr; }
};
bool theme_value_eq(const RolltuiThemePresetValue* a, const RolltuiThemePresetValue* b) {
  return a && b && rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_THEME)->equal(a, b) != 0;
}

class ThemeStore : public StoreBase {
  static RolltuiPresetStore* make(std::string_view dir, bool may_write_shipped, std::string_view shipped_dir) {
    return rolltui_preset_store_new(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_THEME), dir.data(), dir.size(),
                                    may_write_shipped ? 1 : 0, shipped_dir.data(), shipped_dir.size());
  }

 public:
  explicit ThemeStore(std::string dir, bool may_write_shipped = false, std::string shipped_dir = "")
      : StoreBase(make(dir, may_write_shipped, shipped_dir)) {}

  void start(ThemePresetReport& rep) { rolltui_preset_store_start(s_, &rep); }
  ThemeValueHandle working() const { return ThemeValueHandle(s_, rolltui_preset_store_working(s_)); }
  void set_colours(RolltuiJsonValue* colours, bool persist = true) {
    ThemeValueHandle v = working();
    RolltuiThemePresetValue* raw = v.v;
    v.v = nullptr;
    rolltui_json_free(raw->colours);
    raw->colours = colours;
    rolltui_preset_store_set_working(s_, raw, persist ? 1 : 0);
  }
  void set_mode(std::string_view mode, bool persist = true) {
    ThemeValueHandle v = working();
    RolltuiThemePresetValue* raw = v.v;
    v.v = nullptr;
    raw->mode = mode;
    rolltui_preset_store_set_working(s_, raw, persist ? 1 : 0);
  }
  void set_depth(std::string_view depth, bool persist = true) {
    ThemeValueHandle v = working();
    RolltuiThemePresetValue* raw = v.v;
    v.v = nullptr;
    raw->depth = depth;
    rolltui_preset_store_set_working(s_, raw, persist ? 1 : 0);
  }
  ThemeValueHandle get(std::string_view name, ThemePresetReport& rep) const {
    return ThemeValueHandle(s_, rolltui_preset_store_get(s_, name.data(), name.size(), &rep));
  }
  bool load(std::string_view name, ThemePresetReport& rep, bool persist = true) {
    return rolltui_preset_store_load(s_, name.data(), name.size(), &rep, persist ? 1 : 0) != 0;
  }

  static std::vector<std::string> shipped_names() {
    RolltuiStrList names;
    rolltui_preset_shipped_names(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_THEME), &names);
    std::vector<std::string> out;
    for (const RolltuiStr& n : names) out.push_back(n.str());
    return out;
  }
  static bool is_shipped(std::string_view name) { return rolltui_preset_is_shipped(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_THEME), name.data(), name.size()) != 0; }
  static std::string shipped_json(std::string_view name) {
    RolltuiPresetDomain& d = *rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_THEME);
    for (std::size_t i = 0; i < d.shipped_count(); ++i) {
      const char *n = nullptr, *t = nullptr;
      std::size_t nl = 0, tl = 0;
      d.shipped_at(i, &n, &nl, &t, &tl);
      if (std::string_view(n, nl) == name) return std::string(t, tl);
    }
    return "";
  }
  // A BORROW of the domain's parsed cache, valid for the process's life. nullptr when `name`
  // is not shipped.
  static const RolltuiThemePresetValue* shipped(std::string_view name) {
    return static_cast<const RolltuiThemePresetValue*>(
        rolltui_preset_shipped(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_THEME), name.data(), name.size()));
  }
};

// ---- the Layout store -------------------------------------------------------------------------
// `RolltuiLayout` IS `rolltui::Layout` (rolltui_layout.h's own header comment): a plain C++
// value type with real copy/move/destroy/`==` through its members' own (RolltuiStr,
// RolltuiActionList, RolltuiLayer, RolltuiLayerList), so — unlike the Theme and Bindings
// domains' opaque/JSON-backed values — this store can hand back a `RolltuiLayout` BY VALUE.
class LayoutStore : public StoreBase {
  static RolltuiPresetStore* make(std::string_view dir, bool may_write_shipped, std::string_view shipped_dir) {
    return rolltui_preset_store_new(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT), dir.data(), dir.size(),
                                    may_write_shipped ? 1 : 0, shipped_dir.data(), shipped_dir.size());
  }

 public:
  explicit LayoutStore(std::string dir, bool may_write_shipped = false, std::string shipped_dir = "")
      : StoreBase(make(dir, may_write_shipped, shipped_dir)) {}

  void start(LayoutPresetReport& rep) { rolltui_preset_store_start(s_, &rep); }
  RolltuiLayout working() const {
    void* v = rolltui_preset_store_working(s_);
    RolltuiLayout out = *static_cast<RolltuiLayout*>(v);
    rolltui_preset_store_value_free(s_, v);
    return out;
  }
  void set_working(const RolltuiLayout& l, bool persist = true) {
    RolltuiLayout* v = new RolltuiLayout();
    rolltui_layout_copy(v, &l);
    rolltui_preset_store_set_working(s_, v, persist ? 1 : 0);
  }
  std::optional<RolltuiLayout> get(std::string_view name, LayoutPresetReport& rep) const {
    void* v = rolltui_preset_store_get(s_, name.data(), name.size(), &rep);
    if (!v) return std::nullopt;
    RolltuiLayout out = *static_cast<RolltuiLayout*>(v);
    rolltui_preset_store_value_free(s_, v);
    return out;
  }
  bool load(std::string_view name, LayoutPresetReport& rep, bool persist = true) {
    return rolltui_preset_store_load(s_, name.data(), name.size(), &rep, persist ? 1 : 0) != 0;
  }

  static std::vector<std::string> shipped_names() {
    RolltuiStrList names;
    rolltui_preset_shipped_names(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT), &names);
    std::vector<std::string> out;
    for (const RolltuiStr& n : names) out.push_back(n.str());
    return out;
  }
  static std::string shipped_json(std::string_view name) {
    RolltuiPresetDomain& d = *rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT);
    for (std::size_t i = 0; i < d.shipped_count(); ++i) {
      const char *n = nullptr, *t = nullptr;
      std::size_t nl = 0, tl = 0;
      d.shipped_at(i, &n, &nl, &t, &tl);
      if (std::string_view(n, nl) == name) return std::string(t, tl);
    }
    return "";
  }
  static const RolltuiLayout* shipped(std::string_view name) {
    return static_cast<const RolltuiLayout*>(
        rolltui_preset_shipped(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT), name.data(), name.size()));
  }
};

// `builtin_layout(name)`: a SEPARATE code path from the preset domain's `shipped()` above (the
// original C++ had two independent definitions — `rolltui::builtin_layout` in Layout.cpp and
// `PresetStore<LayoutDomain>::shipped` — and one assertion below exists to prove they still
// agree). This mirrors `rolltui-paint`'s own `load_layout_text` exactly: the embedded JSON by
// name, through the standalone loader, never through the preset store.
bool load_layout_text_c(std::string_view text, RolltuiLayout* out, RolltuiLayoutReport* rep) {
  RolltuiLoadedLayout loaded{};
  rolltui_loaded_layout_init(&loaded);
  std::size_t defaults_n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(&defaults_n);
  const bool ok = rolltui_load_layout_text(text.data(), text.size(), &loaded, defaults, defaults_n,
                                           rolltui_layout_default_hooks(), rep) != 0;
  if (ok) rolltui_loaded_layout_to_layout(&loaded, out);
  rolltui_loaded_layout_release(&loaded);
  return ok;
}
const RolltuiLayout* builtin_layout_c(std::string_view name) {
  static const std::vector<std::pair<std::string, RolltuiLayout>> cache = [] {
    std::vector<std::pair<std::string, RolltuiLayout>> out;
    for (std::size_t i = 0; i < rolltui_kLayoutPresetCount; ++i) {
      const RolltuiEmbeddedFile& f = rolltui_kLayoutPresets[i];
      RolltuiLayout l{};
      rolltui_layout_init(&l);
      RolltuiLayoutReport rep{};
      if (load_layout_text_c(f.text, &l, &rep)) out.emplace_back(std::string(f.name), std::move(l));
      rolltui_layout_report_release(&rep);
    }
    return out;
  }();
  for (const auto& [n, l] : cache)
    if (n == name) return &l;
  return nullptr;
}

// ---- the Bindings store -------------------------------------------------------------------------
struct BindingsHandle {
  RolltuiBindings* b;
  explicit BindingsHandle(RolltuiBindings* p) : b(p) {}
  BindingsHandle(const BindingsHandle& o) : b(rolltui_bindings_clone(o.b)) {}
  BindingsHandle(BindingsHandle&& o) noexcept : b(o.b) { o.b = nullptr; }
  BindingsHandle& operator=(BindingsHandle&& o) noexcept {
    if (this != &o) {
      rolltui_bindings_free(b);
      b = o.b;
      o.b = nullptr;
    }
    return *this;
  }
  ~BindingsHandle() { rolltui_bindings_free(b); }
  RolltuiBindings* get() const { return b; }
};
bool bindings_eq(const RolltuiBindings* a, const RolltuiBindings* b) { return rolltui_bindings_equal(a, b) != 0; }
// A const-ref parameter binds (and lifetime-extends) a `working()` temporary, so callers can
// compare two by-value RolltuiLayouts without naming a local for each one.
bool layout_eq(const RolltuiLayout& a, const RolltuiLayout& b) { return rolltui_layout_equal(&a, &b) != 0; }

class BindingsStore : public StoreBase {
  static RolltuiPresetStore* make(std::string_view dir, bool may_write_shipped, std::string_view shipped_dir) {
    return rolltui_preset_store_new(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_BINDINGS), dir.data(), dir.size(),
                                    may_write_shipped ? 1 : 0, shipped_dir.data(), shipped_dir.size());
  }

 public:
  explicit BindingsStore(std::string dir, bool may_write_shipped = false, std::string shipped_dir = "")
      : StoreBase(make(dir, may_write_shipped, shipped_dir)) {}

  void start(BindingsPresetReport& rep) { rolltui_preset_store_start(s_, &rep); }
  BindingsHandle working() const { return BindingsHandle(static_cast<RolltuiBindings*>(rolltui_preset_store_working(s_))); }
  void set_working(BindingsHandle v, bool persist = true) {
    RolltuiBindings* p = v.b;
    v.b = nullptr;
    rolltui_preset_store_set_working(s_, p, persist ? 1 : 0);
  }
  bool load(std::string_view name, BindingsPresetReport& rep, bool persist = true) {
    return rolltui_preset_store_load(s_, name.data(), name.size(), &rep, persist ? 1 : 0) != 0;
  }

  static bool is_shipped(std::string_view name) { return rolltui_preset_is_shipped(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_BINDINGS), name.data(), name.size()) != 0; }
  static std::string shipped_json(std::string_view name) {
    RolltuiPresetDomain& d = *rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_BINDINGS);
    for (std::size_t i = 0; i < d.shipped_count(); ++i) {
      const char *n = nullptr, *t = nullptr;
      std::size_t nl = 0, tl = 0;
      d.shipped_at(i, &n, &nl, &t, &tl);
      if (std::string_view(n, nl) == name) return std::string(t, tl);
    }
    return "";
  }
};

std::optional<RolltuiChord> parse_chord_c(std::string_view s) {
  RolltuiChord c{};
  if (rolltui_chord_parse(s.data(), s.size(), &c)) return c;
  return std::nullopt;
}
std::string action_for_c(const RolltuiBindings* b, const RolltuiChord& k, std::string_view scope) {
  std::size_t n = 0;
  const char* p = rolltui_bindings_action_for(b, &k, scope.data(), scope.size(), &n);
  return p ? std::string(p, n) : std::string();
}

// ---- the Theme resolve/fill fixtures --------------------------------------------------------
struct ThemeFixture {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  ThemeFixture() = default;
  ThemeFixture(const ThemeFixture&) = delete;
  ~ThemeFixture() { rolltui_effect_map_free(effects); }
};
bool builtin_theme_c(std::string_view name, ThemeFixture& out) {
  out.effects = rolltui_theme_builtin_fill(name.data(), name.size(), out.styles, ROLLTUI_ROLE_COUNT);
  return out.effects != nullptr;
}
bool styles_eq(const RolltuiStyle* a, const RolltuiStyle* b) {
  for (std::size_t i = 0; i < ROLLTUI_ROLE_COUNT; ++i)
    if (!(a[i] == b[i])) return false;
  return true;
}
struct ResolvedTheme {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiStr name;
  RolltuiEffectMap* effects = nullptr;
  RolltuiThemeReport report{};
  ResolvedTheme() = default;
  ResolvedTheme(const ResolvedTheme&) = delete;
  ~ResolvedTheme() {
    rolltui_effect_map_free(effects);
    rolltui_theme_report_release(&report);
  }
  bool clean() const {
    return report.error.empty() && report.missing_roles_n == 0 && report.unknown_keys_n == 0 && report.bad_values_n == 0;
  }
};
bool resolve_colours_c(const RolltuiJsonValue* colours, int mode, ResolvedTheme& out) {
  rolltui_effect_map_free(out.effects);
  rolltui_theme_report_release(&out.report);
  out.effects = rolltui_theme_load(colours, mode, rolltui_theme_default_vocab(), out.styles, &out.name, &out.report);
  return out.effects != nullptr;
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
    std::vector<std::string> names = ThemeStore::shipped_names();
    check(!names.empty() && names[0] == "default", "a shipped 'default' always exists and is listed first");
    check(ThemeStore::is_shipped("default") && ThemeStore::is_shipped("mono") && !ThemeStore::is_shipped("mine"), "is_shipped by name");
    // The file that ships is what the embedded table holds, byte for byte.
    for (const std::string& n : names) {
      const std::string on_disk = read_file(fs::path(ROLLTUI_PRESETS_DIR) / (n + ".json"));
      check(!on_disk.empty() && on_disk == ThemeStore::shipped_json(n), "shipped '" + n + "' embeds the file in rolltui/presets/themes verbatim");
    }
    // The shipped colours equal Theme.cpp's built-ins — two definition sites, kept
    // equal by this check (the theme grep control covers only .cpp/.hpp).
    const RolltuiThemePresetValue* d = ThemeStore::shipped("default");
    ResolvedTheme dark, light, mono;
    ThemeFixture builtin_dark, builtin_light, builtin_mono;
    builtin_theme_c("default-dark", builtin_dark);
    builtin_theme_c("default-light", builtin_light);
    builtin_theme_c("mono", builtin_mono);
    const bool ok_dark = resolve_colours_c(d->colours, ROLLTUI_MODE_DARK, dark);
    check(ok_dark && dark.clean() && styles_eq(dark.styles, builtin_dark.styles), "shipped 'default' at dark is the built-in default-dark, role for role");
    const bool ok_light = resolve_colours_c(d->colours, ROLLTUI_MODE_LIGHT, light);
    check(ok_light && light.clean() && styles_eq(light.styles, builtin_light.styles), "…and at light the built-in default-light");
    const RolltuiThemePresetValue* m = ThemeStore::shipped("mono");
    const bool ok_mono = resolve_colours_c(m->colours, ROLLTUI_MODE_DARK, mono);
    check(ok_mono && mono.clean() && styles_eq(mono.styles, builtin_mono.styles), "shipped 'mono' is the built-in mono");
    // Phase 12 m6: the shipped files carry the built-ins' MOTION too. Without this the
    // two definition sites could drift in exactly the way that matters least visibly and
    // most: a built-in that spins and a shipped file — the one every session actually
    // runs — that is silently still.
    check(rolltui_effect_map_equal(dark.effects, builtin_dark.effects) && rolltui_effect_map_equal(light.effects, builtin_light.effects),
          "shipped 'default' carries the built-in's effects at both modes");
    check(rolltui_effect_map_equal(mono.effects, builtin_mono.effects), "…and shipped 'mono' the mono theme's own");
    check(!rolltui_effect_map_empty(dark.effects) && !rolltui_effect_map_empty(mono.effects) && !rolltui_effect_map_equal(dark.effects, mono.effects),
          "…and the two are genuinely different looks, not one map copied twice");
    check(d->mode == "auto" && d->depth == "auto", "shipped 'default' is mode auto, depth auto");
    check(ThemeStore::shipped("default-dark")->mode == "dark" && ThemeStore::shipped("default-light")->mode == "light",
          "'default-dark' / 'default-light' are the same colours pinned to a mode");
    // rolltui_theme_dump(both variants) round-trips both exactly (the shipped file was
    // produced by it) — theme_pair_to_json_value's own port.
    RolltuiJsonValue* pair = rolltui_theme_dump(builtin_dark.styles, builtin_dark.effects, builtin_light.styles, builtin_light.effects,
                                               rolltui_theme_default_vocab());
    ResolvedTheme pd, pl;
    const bool ok_pd = resolve_colours_c(pair, ROLLTUI_MODE_DARK, pd);
    const bool ok_pl = resolve_colours_c(pair, ROLLTUI_MODE_LIGHT, pl);
    check(ok_pd && ok_pl && styles_eq(pd.styles, builtin_dark.styles) && styles_eq(pl.styles, builtin_light.styles),
          "theme_pair_to_json_value loads back to each variant exactly");
    rolltui_json_free(pair);
  }

  // ---- start: no working copy → default (rules 2, 5) ----
  ThemeStore store(dir, false, "");
  {
    ThemePresetReport rep;
    store.start(rep);
    check(rep.clean() && rep.notes_n != 0 && str_of(rep.notes[0]).find("no working copy") == 0,
          "a fresh directory starts from 'default' and says so [" + (rep.notes_n ? str_of(rep.notes[0]) : "") + "]");
    check(store.origin() == "default" && store.label() == "default" && !store.modified(), "label 'default', unmodified");
    check(!fs::exists(store.working_path()), "starting writes nothing");
    check(theme_value_eq(store.working().v, ThemeStore::shipped("default")), "the working copy IS the shipped default (rule 1: the whole domain)");
  }
  // ---- an edit autosaves and changes the label by comparison (rules 2, 4) ----
  {
    const std::uint64_t v0 = store.version();
    store.set_depth("16");
    check(store.version() > v0 && store.label() == "default (modified)" && store.modified(), "a depth change bumps the version and the label reads 'default (modified)'");
    check(fs::exists(store.working_path()), "…and autosaved " + store.working_path());
    RolltuiStr perr{};
    RolltuiJsonValue* v = rolltui_json_parse(read_file(store.working_path()).data(), read_file(store.working_path()).size(), &perr);
    check(v && perr.empty() && std::string_view(rolltui_json_as_string(rolltui_json_get(v, "preset", 6), "", 0, nullptr)) == "default" &&
              std::string_view(rolltui_json_as_string(rolltui_json_get(v, "depth", 5), "", 0, nullptr)) == "16" &&
              rolltui_json_has(v, "colours", 7),
          "the working file records its origin preset and the whole domain");
    check(!rolltui_json_has(v, "layout", 6), "…and NOT a layout: the Theme domain is colours + mode + depth (Phase 10 m1)");
    rolltui_json_free(v);
    rolltui_str_free(&perr);
    check(fs::directory_iterator(dir) != fs::directory_iterator() && !fs::exists(store.working_path() + ".tmp." + std::to_string(::getpid())), "no temp file is left behind (written by rename)");
    store.set_depth("auto");
    check(store.label() == "default" && !store.modified(), "putting the depth back makes it 'default' again — identity is by comparison, not a dirty flag");
    store.set_mode("light");
    check(store.label() == "default (modified)", "a mode change is a modification");
  }
  // ---- restart picks the working copy up (rule 2: nothing is lost) ----
  {
    ThemeStore again(dir, false, "");
    ThemePresetReport rep;
    again.start(rep);
    check(rep.clean() && theme_value_eq(again.working().v, store.working().v) && again.label() == "default (modified)",
          "a second store on the same directory loads the autosaved working copy with its label [" + rep.summary() + "]");
  }
  // ---- save as (rule 3) ----
  {
    RolltuiStr err;
    check(store.save_as("default", false, err) == ROLLTUI_SAVE_REFUSED_SHIPPED, "save-as over a shipped name is refused by name (rule 5): " + err.str());
    check(store.save_as("../evil", false, err) == ROLLTUI_SAVE_BAD_NAME && store.save_as(".hidden", false, err) == ROLLTUI_SAVE_BAD_NAME, "a path-like or dot name is refused");
    check(store.save_as("mine", false, err) == ROLLTUI_SAVE_SAVED && err.empty(), "save-as 'mine' saves");
    check(store.label() == "mine" && store.origin() == "mine" && !store.modified(), "…and the working copy is now 'mine', unmodified");
    check(fs::exists(store.preset_path("mine")), "the preset file exists at " + store.preset_path("mine"));
    RolltuiPresetList list;
    store.list(list);
    bool has_mine = false, shipped_first = !list.empty() && list[0].shipped && list[0].name == "default";
    for (const RolltuiPresetInfo& p : list) if (p.name == "mine" && !p.shipped && p.path == store.preset_path("mine")) has_mine = true;
    check(shipped_first && has_mine, "list() has the shipped presets first and the user preset with its path");
    store.set_depth("256");
    check(store.label() == "mine (modified)", "an edit after saving reads 'mine (modified)'");
    check(store.save_as("mine", false, err) == ROLLTUI_SAVE_EXISTS_ASK, "save-as over an existing user preset asks once: " + err);
    check(store.save_as("mine", true, err) == ROLLTUI_SAVE_SAVED && store.label() == "mine", "…and saves with confirmation");
    // Load copies (rule 3): the preset is read-only — editing the working copy does
    // not touch the file.
    store.set_depth("16");
    ThemePresetReport rep;
    check(store.load("mine", rep) && rep.clean() && store.working()->depth == "256" && store.label() == "mine", "load copies the preset back into the working copy");
    check(store.load("default", rep) && store.label() == "default" && theme_value_eq(store.working().v, ThemeStore::shipped("default")), "load 'default' restores the shipped preset whole");
    check(!store.load("nope", rep) && str_of(rep.error).find("no theme preset 'nope'") == 0, "loading an unknown name fails with a named error [" + str_of(rep.error) + "]");
    check(store.label() == "default", "…and leaves the working copy alone");
  }
  // ---- persist=false: a flag fills the run's copy without writing it ----
  {
    ThemeStore s2((world / "flags").string(), false, "");
    ThemePresetReport rep0;
    s2.start(rep0);
    ThemePresetReport rep;
    check(s2.load("mono", rep, /*persist=*/false) && s2.label() == "mono" && !fs::exists(s2.working_path()),
          "load with persist=false makes the working copy 'mono' in memory and writes nothing");
    s2.set_mode("dark");
    check(fs::exists(s2.working_path()) && s2.label() == "mono (modified)", "the first real edit writes what was running (mono + the edit)");
    ThemeStore s3((world / "flags").string(), false, "");
    ThemePresetReport rep3;
    s3.start(rep3);
    check(s3.origin() == "mono" && s3.working()->mode == "dark", "…and a restart comes back to it");
  }
  // ---- the editor's privilege (rule 5) ----
  {
    const std::string shipped_dir = (world / "shipped").string();
    ThemeStore editor((world / "editor").string(), true, shipped_dir);
    ThemePresetReport rep0;
    editor.start(rep0);
    editor.set_mode("light");
    RolltuiStr err;
    check(editor.save_as("default", false, err) == ROLLTUI_SAVE_SAVED && fs::exists(fs::path(shipped_dir) / "default.json"),
          "with may_write_shipped the editor writes 'default' into the shipped directory: " + err);
    RolltuiStr e2{};
    const std::string written = read_file(fs::path(shipped_dir) / "default.json");
    RolltuiJsonValue* v = rolltui_json_parse(written.data(), written.size(), &e2);
    check(v && e2.empty() && std::string_view(rolltui_json_as_string(rolltui_json_get(v, "mode", 4), "", 0, nullptr)) == "light" &&
              std::string_view(rolltui_json_as_string(rolltui_json_get(v, "name", 4), "", 0, nullptr)) == "default",
          "…as a complete preset file");
    rolltui_json_free(v);
    rolltui_str_free(&e2);
    check(editor.label() == "default", "…and the working copy is 'default' again (the file it just wrote)");
  }
  // ---- a broken working copy is reported, not served ----
  {
    const std::string bad = (world / "bad").string();
    write_file(fs::path(bad) / "theme.working.json", "{ not json");
    ThemeStore s(bad, false, "");
    ThemePresetReport rep;
    s.start(rep);
    check(!rep.error.empty() && str_of(rep.error).find("unreadable") != std::string::npos && s.label() == "default", "an unparseable working copy: error named, default served");
    write_file(fs::path(bad) / "theme.working.json", R"({"mode":"sideways","depth":"256","colours":{"roles":{"text":{"fg":"none"}}},"extra":1})");
    ThemeStore s4(bad, false, "");
    ThemePresetReport rep4;
    s4.start(rep4);
    check(rep4.error.empty() && rep4.bad_values_n == 1 && str_of(rep4.bad_values[0]).find("mode") == 0 && rep4.unknown_keys_n == 1 &&
              rep4.unknown_keys[0] == "extra" && rep4.colours.missing_roles_n == ROLLTUI_ROLE_COUNT - 1,
          "a loadable working copy with problems loads and reports each: bad mode, unknown key, missing roles [" + rep4.summary() + "]");
    check(s4.working()->mode == "auto" && s4.working()->depth == "256", "the bad value keeps its default; the good parts load");
    // THE SENTENCE ITSELF, not just its inputs (Phase 17 m2a, +1 assertion). Every use of
    // summary() in this suite was inside a check NAME — so the composition it performs (which
    // parts, in what order, "bad: "/"unknown: "/"colours: " prefixes, "; " between them) was
    // asserted by nothing at all, and moving it to C could have changed every word silently.
    {
      const std::string one = rep4.summary();
      check(one.find("bad: mode") == 0 && one.find("; unknown: extra") != std::string::npos &&
                one.find("; colours: " + std::to_string(ROLLTUI_ROLE_COUNT - 1) + " roles missing (inherit text)") != std::string::npos,
            "…and summary() composes them in order with their prefixes [" + one + "]");
    }
  }
  // ---- colours-only theme files ----
  {
    const std::string d2 = (world / "files").string();
    ThemeStore s(d2, false, "");
    ThemePresetReport rep0;
    s.start(rep0);
    s.set_depth("mono");
    ThemeFixture mono;
    builtin_theme_c("mono", mono);
    RolltuiJsonValue* dump = rolltui_theme_dump(mono.styles, mono.effects, nullptr, nullptr, rolltui_theme_default_vocab());
    RolltuiStr text{};
    rolltui_json_dump(dump, 2, &text);
    write_file(fs::path(d2) / "colours.json", str_of(text));
    rolltui_str_free(&text);
    rolltui_json_free(dump);
    ThemePresetReport rep;
    check(s.load((fs::path(d2) / "colours.json").string(), rep) && rep.notes_n != 0 && str_of(rep.notes[rep.notes_n - 1]).find("colours-only") != std::string::npos,
          "a colours-only theme file loads into the colours part with a note");
    ResolvedTheme tr;
    check(resolve_colours_c(s.working()->colours, ROLLTUI_MODE_DARK, tr) && styles_eq(tr.styles, mono.styles) && s.working()->depth == "mono" && s.label() == "default (modified)",
          "…the look is mono, mode and depth kept, and the label says modified against 'default'");
    // A user preset saved under a shipped name's file is never listed as a user preset.
    write_file(fs::path(d2) / "themes" / "default.json", "{}");
    bool dup = false;
    RolltuiPresetList sl;
    s.list(sl);
    for (const RolltuiPresetInfo& p : sl) if (p.name == "default" && !p.shipped) dup = true;
    check(!dup, "a user file named like a shipped preset does not shadow or duplicate it");
  }
  // ---- the file format, round trip ----
  {
    const RolltuiThemePresetValue* d = ThemeStore::shipped("default");
    ThemePresetReport rep;
    RolltuiJsonValue* dumped = rolltui_theme_preset_to_json(rolltui_json_clone(d->colours), d->mode.data(), d->mode.size(),
                                                            d->depth.data(), d->depth.size(), "default", 7);
    const RolltuiJsonValue* colours_c = nullptr;
    RolltuiStr mode{}, depth{};
    int ok = rolltui_theme_preset_parse(dumped, rolltui_theme_default_vocab(), rolltui_theme_mode_setting_valid,
                                        rolltui_color_depth_setting_valid, &mode, &depth, &colours_c, &rep);
    check(ok && rep.clean() && mode == d->mode && depth == d->depth && rolltui_json_equal(colours_c, d->colours),
          "theme_preset_to_json / theme_preset_from_json round-trips the whole domain");
    rolltui_str_free(&mode);
    rolltui_str_free(&depth);
    rolltui_json_free(dumped);

    RolltuiJsonValue* no_colours = rolltui_json_object();
    RolltuiStr mode2{}, depth2{};
    const RolltuiJsonValue* colours2 = nullptr;
    check(!rolltui_theme_preset_parse(no_colours, rolltui_theme_default_vocab(), rolltui_theme_mode_setting_valid,
                                      rolltui_color_depth_setting_valid, &mode2, &depth2, &colours2, &rep) &&
              str_of(rep.error).find("colours") != std::string::npos,
          "a preset without colours is unusable");
    rolltui_str_free(&mode2);
    rolltui_str_free(&depth2);
    rolltui_json_free(no_colours);

    RolltuiJsonValue* whole = rolltui_theme_preset_to_json(rolltui_json_clone(d->colours), d->mode.data(), d->mode.size(),
                                                           d->depth.data(), d->depth.size(), "x", 1);
    check(!rolltui_json_has(whole, "layout", 6), "a written theme preset carries no layout (Phase 10 m1)");
    // A theme file carrying a "layout" key: since 2026-09-05 it is an ORDINARY UNKNOWN
    // KEY, not a special case. The Theme domain used to name it and pass the file clean, on
    // the strength of a Phase 9 -> Phase 10 migration that has since been retired; with the
    // migration gone there is nothing that key can mean, so it is reported like any other.
    const RolltuiLayout* stacked = builtin_layout_c("stacked");
    RolltuiJsonValue* stacked_json =
        rolltui_layout_to_json_value(stacked->name.data(), stacked->name.size(), stacked->min_width, stacked->min_height,
                                     stacked->actions.data(), stacked->actions.size(), &stacked->base, stacked->popups.data(),
                                     stacked->popups.size(), rolltui_layout_default_hooks());
    rolltui_json_set(whole, "layout", 6, stacked_json);
    const RolltuiJsonValue* colours3 = nullptr;
    RolltuiStr mode3{}, depth3{};
    ok = rolltui_theme_preset_parse(whole, rolltui_theme_default_vocab(), rolltui_theme_mode_setting_valid,
                                    rolltui_color_depth_setting_valid, &mode3, &depth3, &colours3, &rep);
    check(ok && rolltui_json_equal(colours3, d->colours) && mode3 == d->mode && depth3 == d->depth && !rep.clean() &&
              rep.unknown_keys_n == 1 && str_of(rep.unknown_keys[0]) == "layout" && rep.notes_n == 0,
          "a theme file carrying a \"layout\" key still loads its colours and reports the key as unknown [" + rep.summary() + "]");
    rolltui_str_free(&mode3);
    rolltui_str_free(&depth3);
    rolltui_json_free(whole);
  }
  // ---- precedence: flag > env > working > builtin, all 8 present/absent combinations ----
  {
    bool all = true;
    for (int mask = 0; mask < 8; ++mask) {
      const std::string_view flag = (mask & 4) ? "F" : "", env = (mask & 2) ? "E" : "", working = (mask & 1) ? "W" : "";
      const char* value = nullptr;
      std::size_t value_len = 0;
      RolltuiPresetRung rung{};
      rolltui_preset_resolve_setting(flag.data(), flag.size(), env.data(), env.size(), working.data(), working.size(), "B", 1,
                                     &value, &value_len, &rung);
      const std::string got(value, value_len);
      const std::string want_value = (mask & 4) ? "F" : (mask & 2) ? "E" : (mask & 1) ? "W" : "B";
      const RolltuiPresetRung want_rung = (mask & 4) ? ROLLTUI_PRESET_RUNG_FLAG : (mask & 2) ? ROLLTUI_PRESET_RUNG_ENV
                                          : (mask & 1)                          ? ROLLTUI_PRESET_RUNG_WORKING
                                                                                : ROLLTUI_PRESET_RUNG_BUILTIN;
      if (!(got == want_value && rung == want_rung)) {
        all = false;
        std::size_t rn = 0;
        check(false, "precedence mask " + std::to_string(mask) + ": got " + got + " from " + std::string(rolltui_preset_rung_name(rung, &rn), rn));
      }
    }
    check(all, "the first non-empty rung wins in every one of the 8 combinations, and the answer names its rung");
    std::size_t fn = 0, bn = 0;
    check(std::string_view(rolltui_preset_rung_name(ROLLTUI_PRESET_RUNG_FLAG, &fn), fn) == "flag" &&
              std::string_view(rolltui_preset_rung_name(ROLLTUI_PRESET_RUNG_BUILTIN, &bn), bn) == "built-in default",
          "rung names");
    auto setting = [](std::string_view key) -> const RolltuiPresetSettingSpec* {
      const int i = rolltui_preset_setting_index(key.data(), key.size());
      return i < 0 ? nullptr : rolltui_preset_settings_at(static_cast<std::size_t>(i));
    };
    check(setting("theme") && setting("layout") && setting("theme_mode") && setting("color_depth") && setting("bindings") && !setting("frontend"),
          "one table holds all five settings across the three domains (frontend is a host's)");
    std::size_t ln = 0;
    check(setting("theme")->domain == ROLLTUI_PRESET_DOMAIN_THEME && setting("layout")->domain == ROLLTUI_PRESET_DOMAIN_LAYOUT &&
              setting("bindings")->domain == ROLLTUI_PRESET_DOMAIN_BINDINGS && setting("theme_mode")->domain == ROLLTUI_PRESET_DOMAIN_THEME &&
              std::string_view(rolltui_preset_domain_name(ROLLTUI_PRESET_DOMAIN_LAYOUT, &ln), ln) == "layout",
          "…each row names its own domain (Phase 10 m1: 'layout' is no longer a Theme row)");
    check(std::string_view(setting("theme")->builtin, setting("theme")->builtin_len) == "default" &&
              std::string_view(setting("layout")->builtin, setting("layout")->builtin_len) == "default" &&
              std::string_view(setting("theme_mode")->builtin, setting("theme_mode")->builtin_len) == "auto" &&
              std::string_view(setting("color_depth")->builtin, setting("color_depth")->builtin_len) == "auto",
          "built-in defaults: default / default / auto / auto");
    ThemePresetReport lrep;
    store.load("default", lrep);  // (the store from above)
    store.set_mode("dark");
    check(store.working_value("theme") == "default" && store.working_value("theme_mode") == "dark" && store.working_value("color_depth") == "auto" &&
              store.working_value("layout").empty(),
          "working_value reads each of the Theme domain's settings, and none of another domain's");
  }
  // ---- the Layout domain (Phase 10 m1): the same five rules on the third domain ----
  {
    const std::string ldir = (world / "layouts").string();
    LayoutStore ls(ldir, false, "");
    LayoutPresetReport rep;
    ls.start(rep);
    check(rep.clean() && ls.label() == "default" && layout_eq(ls.working(), *builtin_layout_c("default")),
          "a fresh directory starts from the shipped default layout (rule 5), label 'default'");
    // The shipped presets ARE the built-ins: one definition site, asserted anyway.
    std::vector<std::string> names = LayoutStore::shipped_names();
    check(names.size() == 4 && names[0] == "default", "four shipped layouts, 'default' first");
    bool all_builtin = true;
    for (const std::string& n : names) {
      const std::string on_disk = read_file(fs::path(ROLLTUI_LAYOUTS_DIR) / (n + ".json"));
      if (on_disk.empty() || on_disk != LayoutStore::shipped_json(n)) { all_builtin = false; check(false, "shipped layout '" + n + "' does not embed its file verbatim"); }
      if (!builtin_layout_c(n) || !rolltui_layout_equal(LayoutStore::shipped(n), builtin_layout_c(n))) { all_builtin = false; check(false, "shipped layout '" + n + "' differs from builtin_layout()"); }
    }
    check(all_builtin, "every shipped layout embeds rolltui/presets/layouts/<name>.json verbatim AND is builtin_layout(name) — one definition site");
    // Rule 2: one working copy, autosaved; rule 4: the label by comparison.
    RolltuiLayout wide;
    rolltui_layout_copy(&wide, builtin_layout_c("default"));
    wide.base.root.children[1].size = RolltuiSplitSize::fixed(RolltuiDim::abs(48));  // a wider status panel
    ls.set_working(wide);
    check(ls.label() == "default (modified)" && fs::exists(ls.working_path()) && ls.working_path() == ldir + "/layout.working.json",
          "an edit autosaves layout.working.json and reads 'default (modified)'");
    LayoutStore again(ldir, false, "");
    LayoutPresetReport rep2;
    again.start(rep2);
    check(rep2.clean() && layout_eq(again.working(), wide) && again.label() == "default (modified)",
          "a restart loads the autosaved working copy exactly, with its label [" + rep2.summary() + "]");
    // Rules 3 and 5.
    RolltuiStr err;
    check(ls.save_as("default", false, err) == ROLLTUI_SAVE_REFUSED_SHIPPED, "save-as over a shipped layout name is refused (rule 5)");
    check(ls.save_as("wide", false, err) == ROLLTUI_SAVE_SAVED && ls.label() == "wide" && fs::exists(ls.preset_path("wide")), "save-as 'wide' saves and becomes the origin");
    check(ls.load("stacked", rep) && layout_eq(ls.working(), *builtin_layout_c("stacked")) && ls.label() == "stacked", "load copies a shipped layout back whole (rule 1)");
    check(ls.load("wide", rep) && layout_eq(ls.working(), wide), "…and the user preset back");
    // A file dropped into <dir>/layouts is a preset: the Phase 9 discovery, by the
    // domain's own mechanics now.
    const RolltuiLayout* no_panel = builtin_layout_c("no-panel");
    RolltuiStr np_text{};
    rolltui_layout_to_json_text(no_panel->name.data(), no_panel->name.size(), no_panel->min_width, no_panel->min_height,
                                no_panel->actions.data(), no_panel->actions.size(), &no_panel->base, no_panel->popups.data(),
                                no_panel->popups.size(), rolltui_layout_default_hooks(), &np_text);
    write_file(fs::path(ldir) / "layouts" / "two.json", str_of(np_text));
    rolltui_str_free(&np_text);
    bool has_two = false;
    RolltuiPresetList ll;
    ls.list(ll);
    for (const RolltuiPresetInfo& p : ll) if (p.name == "two" && !p.shipped) has_two = true;
    check(has_two, "a layout file dropped into <dir>/layouts is listed as a preset by name");
    std::optional<RolltuiLayout> l = ls.get("two", rep);
    check(l && rep.clean() && l->name == "no-panel", "…and loads (its own \"name\" and the file name may differ)");
    check(ls.get((fs::path(ldir) / "layouts" / "two.json").string(), rep).has_value(), "…and resolves by path too");
    check(!ls.get("nothing", rep) && str_of(rep.error).find("no layout preset 'nothing'") == 0, "an unknown name is a named error [" + str_of(rep.error) + "]");
    check(!ls.load("nothing", rep) && ls.label() == "wide", "…and leaves the working copy alone");
    // A layout file's own problems are reported through the layout sub-report.
    write_file(fs::path(ldir) / "layouts" / "odd.json", R"({"name":"odd","colour":"blue","root":{"content":"transcript:session","border":"triple"}})");
    check(ls.load("odd", rep) && !rep.clean() && rep.layout.unknown_keys_n == 1 && rep.layout.unknown_keys[0] == "colour" && rep.layout.bad_values_n == 1 &&
              ls.working().base.root.border == rolltui::Border::None,
          "a layout with an unknown key and a bad value loads, reports both, and keeps the default [" + rep.summary() + "]");
    check(ls.working_value("layout") == "odd" && ls.working_value("theme").empty(), "working_value on the Layout store is its origin, and nothing else's");
    rolltui_layout_release(&wide);
  }
  // ---- the Bindings domain (milestone 17): the same five rules on the second domain ----
  {
    const std::string bdir = (world / "bindings").string();
    BindingsStore bs(bdir, false, "");
    BindingsPresetReport rep;
    bs.start(rep);
    check(rep.clean() && bs.label() == "default" && bindings_eq(bs.working().get(), rolltui_bindings_default()), "a fresh directory starts from the shipped default bindings (rule 5), label 'default'");
    check(BindingsStore::is_shipped("default") && BindingsStore::shipped_json("default") == default_bindings_json_c(),
          "the shipped 'default' is the embedded file, verbatim");
    BindingsHandle vim = bs.working();
    const RolltuiChord alt_b = *parse_chord_c("alt+b"), alt_f = *parse_chord_c("alt+f");
    rolltui_bindings_bind(vim.get(), "input.word_left", 15, &alt_b, nullptr, nullptr);
    rolltui_bindings_bind(vim.get(), "input.word_right", 16, &alt_f, nullptr, nullptr);
    bs.set_working(std::move(vim));
    check(bs.label() == "default (modified)" && fs::exists(bs.working_path()) && action_for_c(bs.working().get(), *parse_chord_c("alt+b"), "input") == "input.word_left",
          "an edit autosaves bindings.working.json and the label reads 'default (modified)' (rules 2, 4)");
    BindingsStore again(bdir, false, "");
    BindingsPresetReport rep2;
    again.start(rep2);
    check(rep2.clean() && bindings_eq(again.working().get(), bs.working().get()) && again.label() == "default (modified)", "a restart loads the autosaved working copy with its label [" + rep2.summary() + "]");
    RolltuiStr err;
    check(bs.save_as("default", false, err) == ROLLTUI_SAVE_REFUSED_SHIPPED, "save-as over the shipped name is refused (rule 5)");
    check(bs.save_as("vim-ish", false, err) == ROLLTUI_SAVE_SAVED && bs.label() == "vim-ish" && fs::exists(bs.preset_path("vim-ish")), "save-as 'vim-ish' saves (rule 3) and becomes the origin");
    check(bs.load("default", rep) && bindings_eq(bs.working().get(), rolltui_bindings_default()) && bs.label() == "default", "load copies the shipped default back (rule 1: the whole domain)");
    check(bs.load("vim-ish", rep) && action_for_c(bs.working().get(), *parse_chord_c("alt+f"), "input") == "input.word_right", "…and the user preset back");
    // A file that moves Enter is refused by name in the load report; the rest loads.
    write_file(fs::path(bdir) / "bindings" / "bad.json", R"({"name":"bad","bindings":{"input.submit":["ctrl+j"],"input.newline":["enter"],"input.left":["hyper+x"]}})");
    check(bs.load("bad", rep) && !rep.clean() && rep.bindings.bad_values_n == 2 && str_of(rep.bindings.bad_values[0]).find("input.newline: 'enter' is always input.submit") == 0 &&
              rep.bindings.bad_chords_n == 1 && action_for_c(bs.working().get(), *parse_chord_c("enter"), "input") == "input.submit",
          "a file binding Enter elsewhere loads with Enter refused by name and restored on submit [" + rep.summary() + "]");
    check(!bs.load("nothing", rep) && str_of(rep.error).find("no bindings preset 'nothing'") == 0, "an unknown bindings preset is a named error");
    const int bidx = rolltui_preset_setting_index("bindings", 8);
    const RolltuiPresetSettingSpec* bspec = bidx < 0 ? nullptr : rolltui_preset_settings_at(static_cast<std::size_t>(bidx));
    check(bs.working_value("bindings") == "bad" && bspec && bspec->domain == ROLLTUI_PRESET_DOMAIN_BINDINGS &&
              std::string_view(bspec->builtin, bspec->builtin_len) == "default",
          "the Bindings domain's one setting: 'bindings', built-in 'default'");
    // All three domains in one directory, three working files, none touching another.
    ThemeStore ts(bdir, false, "");
    ThemePresetReport tsr;
    ts.start(tsr);
    ts.set_mode("light");
    LayoutStore ls(bdir, false, "");
    LayoutPresetReport lsr;
    ls.start(lsr);
    ls.load("no-panel", lsr);
    check(fs::exists(fs::path(bdir) / "theme.working.json") && fs::exists(fs::path(bdir) / "layout.working.json") && fs::exists(fs::path(bdir) / "bindings.working.json") &&
              bs.label() == "bad" && ts.label() == "default (modified)" && ls.label() == "no-panel",
          "a session is Theme X + Layout Y + Bindings Z: three working copies side by side, each with its own label");
  }

  // ---- OSC 11 ----
  {
    RolltuiStyleColor c{};
    check(rolltui_parse_osc11_reply("\x1b]11;rgb:1414/1616/1a1a\x1b\\", 26, &c) && c == RolltuiStyleColor::rgb(0x14, 0x16, 0x1a),
          "a 16-bit-per-channel reply (ST-terminated) parses to its top bytes");
    check(rolltui_parse_osc11_reply("\x1b]11;rgb:ffff/ffff/ffff\a", 24, &c) && c == RolltuiStyleColor::rgb(255, 255, 255), "a BEL-terminated reply parses");
    static const char kJunk[] = "junk\x1b]11;rgb:f/8/0\x1b\\more";
    check(rolltui_parse_osc11_reply(kJunk, sizeof(kJunk) - 1, &c) && c == RolltuiStyleColor::rgb(255, 136, 0),
          "1-digit channels scale (f → 255, 8 → 136); surrounding bytes are ignored");
    check(!rolltui_parse_osc11_reply("\x1b]11;?\x1b\\", 8, &c) && !rolltui_parse_osc11_reply("\x1b]11;rgb:zz/00/00\x1b\\", 20, &c) &&
              !rolltui_parse_osc11_reply("hello", 5, &c),
          "the query itself, a bad digit and no reply at all are nullopt");
    check(rolltui_mode_for_background(RolltuiStyleColor::rgb(0x14, 0x16, 0x1a)) == ROLLTUI_MODE_DARK &&
              rolltui_mode_for_background(RolltuiStyleColor::rgb(0xfa, 0xfa, 0xf8)) == ROLLTUI_MODE_LIGHT,
          "a near-black background is dark, a near-white one light");
    check(rolltui_mode_for_background(RolltuiStyleColor::rgb(0x80, 0x80, 0x80)) == ROLLTUI_MODE_DARK &&
              rolltui_mode_for_background(RolltuiStyleColor::rgb(0xc0, 0xc0, 0xc0)) == ROLLTUI_MODE_LIGHT,
          "mid grey (#808080, luminance 0.22) is dark; #c0c0c0 (0.53) is light — the threshold is relative luminance 0.5");
    check(rolltui_mode_for_background(RolltuiStyleColor::none()) == ROLLTUI_MODE_DARK, "no colour answer: dark");
    check(rolltui_theme_mode_from_name("light", 5) == ROLLTUI_MODE_LIGHT && rolltui_theme_mode_from_name("auto", 4) < 0 &&
              rolltui_color_depth_from_name("256", 3) == ROLLTUI_DEPTH_ANSI256 && rolltui_color_depth_from_name("auto", 4) < 0,
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
  // PHASE 17 m2c / 18 m3: this asserts the SAME descriptors this whole file uses throughout —
  // the library's own, `rolltui_preset_domain(...)`, built through `rolltui_theme_preset_domain_init`
  // and its siblings. What the section verifies stands on its own: the library's domain fills
  // every slot the store calls through, a clone survives its original, and a NULL report is
  // safe on the failing path.
  {
    RolltuiPresetDomain& d = *rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_THEME);
    check(d.parse && d.to_json && d.clone && d.destroy && d.equal && d.shipped_at, "the C theme domain fills every slot the store calls through");
    check(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT)->parse && rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT)->clone && rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT)->destroy && rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_LAYOUT)->equal, "…and so does the C layout domain");
    check(rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_BINDINGS)->parse && rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_BINDINGS)->clone && rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_BINDINGS)->destroy && rolltui_preset_domain(ROLLTUI_PRESET_DOMAIN_BINDINGS)->equal, "…and the C bindings domain");
    // A CLONE MUST SURVIVE ITS ORIGINAL, which is the exact bug ASan caught during the port:
    // theme_domain_clone allocated without zeroing, and rolltui_str_set then read the
    // uninitialised RolltuiStr as if it were valid. A clone that is merely allocated is not a
    // clone, so this parses one, clones it, destroys the original and reads the copy.
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
