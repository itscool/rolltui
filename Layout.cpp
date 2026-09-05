// rolltui/Layout.cpp — the SHIM over `rolltui/c/rolltui_layout.h`: RAII (what little of it
// this module still needs), and the conversion at the few places a caller genuinely needs
// `rolltui::ActionDecl` (Bindings.hpp's own `std::string` shape) rather than the C forms.
// `rolltui_layout.c` is the ONLY implementation (this file's one-time C++ counterpart,
// `LayoutCpp.cpp`, is one of the sixteen `*Cpp.cpp` files CMakeLists.txt records as deleted
// once `-DROLLTUI_C` — Phase 15 m5's two-implementation rollback flag — was spent,
// 2026-09-04); this file is the C++ API over it.
//
// PHASE 17 (this task): `rolltui::Layout`, `rolltui::Content` and `WidgetKind` ARE their C
// forms now (`RolltuiLayout`, `RolltuiContent`, `rolltui::WidgetKind` — Layout.hpp's `using`
// aliases), the same one-definition rule `Node`/`Layer` already used. `Layout::actions` is a
// `RolltuiActionList` and `Layout::popups` a `RolltuiLayerList` — count/at/add/remove handles
// over `RolltuiLayoutAction`/`RolltuiLayer` values — rather than `std::vector`, which is what
// makes `Layout` a real C struct instead of a shim over one. The one thing this still bridges
// is `rolltui::ActionDecl` (Bindings.hpp): it stays its own `std::string`-shaped type for the
// many callers this task does not touch (`Bindings::declare`, `AppProfile::actions`), so
// `action_decls()` below is the one seam that builds a `std::vector<ActionDecl>` from a
// `RolltuiActionList` where a caller still needs one. This file's other job is unchanged:
// build a `RolltuiLayoutHooks` bridging Role's vocabulary (`Style.hpp`'s, never a C file's —
// the m2 rule at `rolltui_diff.h`) and Bindings' "which scopes are the library's", and unpack
// the transient `RolltuiLoadedLayout`/`RolltuiLayoutReport` the C loader fills, once per load.
#include "rolltui/Layout.hpp"
#include "rolltui/c/rolltui_embedded.h"

#include "rolltui/Lifetime.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "rolltui/Json.hpp"
#include "rolltui/Scratch.hpp"

namespace rolltui::json {
// Defined in Json.cpp, with external linkage there for exactly this reason (Phase 17 m2):
// `load_layout(const Value&, …)` and `layout_to_json_value` still cross a `json::Value` too
// (`Presets.cpp` embeds a layout inside a bigger preset document), and forward-declaring the
// one tree-conversion implementation Json.cpp already has is what keeps there being exactly
// one, rather than a second copy of it living here. Not declared in `Json.hpp` itself — that
// header's public shape stays exactly what the other six C++ modules already see.
RolltuiJsonValue* value_to_c(const Value& v);
Value value_from_c(const RolltuiJsonValue* v);
}  // namespace rolltui::json

// ---- RolltuiActionList: the C++ edge (Phase 17) -----------------------------------------------
// `RolltuiActionList` (rolltui_layout.h) is declared at GLOBAL scope, like every `Rolltui*` C
// struct, so its out-of-line members are defined here — OUTSIDE `namespace rolltui` below —
// exactly where `LayoutTree.cpp` defines `RolltuiNodeList`'s and `RolltuiLayer`'s for the same
// reason. The five special members a raw `v` needs (it has no value semantics of its own,
// unlike `RolltuiStr`/`RolltuiLayer`) and the methods that call a C function are declared in
// the header and defined here rather than inline, for the same reason `RolltuiNodeList`'s
// equivalents are out-of-line in `LayoutTree.cpp`: an inline body inside the struct is parsed
// in a complete-class context for MEMBER names, but an ordinary (non-member) name like
// `rolltui_action_list_release` still needs to be declared by that point in the file, and
// these functions are declared AFTER the struct, not before it.

RolltuiActionList::~RolltuiActionList() { rolltui_action_list_release(this); }

void RolltuiActionList::copy_from(const RolltuiActionList& o) { rolltui_action_list_copy(this, &o); }

RolltuiActionList& RolltuiActionList::operator=(RolltuiActionList&& o) noexcept {
  if (this != &o) {
    rolltui_action_list_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}

void RolltuiActionList::push_back(const RolltuiLayoutAction& a) {
  RolltuiLayoutAction* p = rolltui_action_list_add(this);
  rolltui_str_set(&p->name, a.name.p, a.name.n);
  rolltui_str_set(&p->description, a.description.p, a.description.n);
}

void RolltuiActionList::erase_name(std::string_view name) {
  rolltui_action_list_remove_name(this, name.data(), name.size());
}

void RolltuiActionList::clear() { rolltui_action_list_clear(this); }

bool RolltuiActionList::operator==(const RolltuiActionList& o) const {
  return rolltui_action_list_equal(this, &o) != 0;
}

namespace rolltui {

// ---- content: the widget kind and its source ----------------------------------------------
// Rung 1 and rung 2 both live in the C (`rolltui_layout.h`); what a Content IS lives here
// (Layout.hpp's own std::string shape — see this file's header comment), and every SENTENCE
// about one now lives in the C too (`rolltui_content_parse`/`_format`, Phase 17 m2).

namespace {

std::string_view kind_name_at(std::size_t i) {
  std::size_t n = 0;
  const char* p = rolltui_widget_kind_library_name(i, &n);
  return std::string_view(p, n);
}

// The one place the enum and the C table are tied together, and it is CHECKED rather than
// assumed: the table's order IS `WidgetKind`'s, and `Registered` is the one value with no
// row by design.
constexpr std::size_t kLibraryKindCount = static_cast<std::size_t>(WidgetKind::Registered);

}  // namespace

std::string_view widget_kind_name(WidgetKind k) {
  const std::size_t i = static_cast<std::size_t>(k);
  return i < kLibraryKindCount ? kind_name_at(i) : std::string_view();
}

std::string_view content_kind_name(const Content& c) {
  return c.kind == WidgetKind::Registered ? std::string_view(c.registered_name) : widget_kind_name(c.kind);
}

std::optional<WidgetKind> widget_kind_from_name(std::string_view name) {
  unsigned char ordinal = 0;
  if (rolltui_widget_kind_resolve(name.data(), name.size(), &ordinal, nullptr, nullptr, nullptr) ==
      ROLLTUI_KIND_LIBRARY)
    return static_cast<WidgetKind>(ordinal);
  return std::nullopt;
}

SourceRule source_rule(WidgetKind k) {
  const std::size_t i = static_cast<std::size_t>(k);
  return static_cast<SourceRule>(rolltui_widget_kind_library_rule(i < kLibraryKindCount ? i : 0));
}

SourceRule content_source_rule(const Content& c) {
  if (c.kind != WidgetKind::Registered) return source_rule(c.kind);
  unsigned char rule = ROLLTUI_SOURCE_REQUIRED;
  rolltui_widget_kind_resolve(c.registered_name.data(), c.registered_name.size(), nullptr, &rule, nullptr,
                              nullptr);
  return static_cast<SourceRule>(rule);
}

std::string_view source_describes(WidgetKind k) {
  std::size_t n = 0;
  const std::size_t i = static_cast<std::size_t>(k);
  const char* p = rolltui_widget_kind_library_source_is(i < kLibraryKindCount ? i : 0, &n);
  return std::string_view(p, n);
}

std::string content_source_describes(const Content& c) {
  if (c.kind != WidgetKind::Registered) return std::string(source_describes(c.kind));
  const char* p = nullptr;
  std::size_t n = 0;
  if (rolltui_widget_kind_resolve(c.registered_name.data(), c.registered_name.size(), nullptr, nullptr, &p, &n) ==
      ROLLTUI_KIND_HOST)
    return std::string(p, n);
  return {};
}

// Rung 1 is checked FIRST and the refusal says so by name: the library's own kinds may
// never be shadowed, and this is one of the two independent guards (the other is that
// the C searches its table before the host's, so a shadowing row could not be reached
// even if one existed).
bool register_widget_kind(std::string name, SourceRule rule, std::string source_is, std::string* why) {
  const int verdict = rolltui_widget_kind_register(name.data(), name.size(), static_cast<unsigned char>(rule),
                                                   source_is.data(), source_is.size());
  if (verdict == ROLLTUI_REGISTER_OK) return true;
  if (why) switch (verdict) {
      case ROLLTUI_REGISTER_EMPTY: *why = "a widget kind needs a name"; break;
      case ROLLTUI_REGISTER_HAS_COLON:
        *why = "'" + name + "' is not a kind name: a ':' separates the kind from its source";
        break;
      case ROLLTUI_REGISTER_IS_LIBRARY:
        *why = "'" + name + "' is one of the library's own kinds and cannot be registered over";
        break;
      default: *why = "'" + name + "' is already registered with a different source rule"; break;
    }
  return false;
}

void clear_registered_widget_kinds() { rolltui_widget_kind_clear(); }

std::vector<std::string> widget_kind_names() {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < rolltui_widget_kind_library_count(); ++i) out.emplace_back(kind_name_at(i));
  for (std::size_t i = 0; i < rolltui_widget_kind_host_count(); ++i) {
    std::size_t n = 0;
    const char* p = rolltui_widget_kind_host_name(i, &n);
    out.emplace_back(p, n);
  }
  return out;
}

const std::vector<WidgetKind>& widget_kinds() {
  static const std::vector<WidgetKind> all = [] {
    std::vector<WidgetKind> v;
    for (std::size_t i = 0; i < kLibraryKindCount; ++i) v.push_back(static_cast<WidgetKind>(i));
    return v;
  }();
  return all;
}

// ---- Role and scope: the two callbacks a C loader/dumper cannot name itself ---------------
// Role's vocabulary is `Style.hpp`'s (`rolltui_layout_tree.h`'s own rule: "this file names no
// role"), and "which scopes are the library's" is Bindings' (`rolltui_bindings.h`'s own rule).
// `RolltuiLayoutHooks` is how the C loader/dumper reaches back for both without carrying
// either table itself; these two functions and the one constant below are the whole bridge.
namespace {

int role_from_name_cb(void*, const char* name, std::size_t len, unsigned char* out) {
  const Role r = role_from_name(std::string_view(name, len));
  if (r == Role::count_) return 0;
  *out = static_cast<unsigned char>(r);
  return 1;
}

std::size_t role_name_cb(void*, unsigned char role, char* out, std::size_t cap) {
  const std::string_view name = role_name(static_cast<Role>(role));
  std::size_t n = name.size();
  if (n >= cap) n = cap ? cap - 1 : 0;
  if (cap) {
    std::memcpy(out, name.data(), n);
    out[n] = '\0';
  }
  return n;
}

int is_library_scope_cb(void*, const char* scope, std::size_t len) {
  return library_scope(std::string_view(scope, len)) ? 1 : 0;
}

constexpr RolltuiLayoutHooks kHooks = {
    /*is_library_scope=*/is_library_scope_cb,
    /*scope_ctx=*/nullptr,
    /*role_from_name=*/role_from_name_cb,
    /*role_from_name_ctx=*/nullptr,
    /*role_name=*/role_name_cb,
    /*role_name_ctx=*/nullptr,
};

}  // namespace

// ---- content: parsing and formatting — the C's algorithm and English; `Content` itself is
// `RolltuiContent` now (Layout.hpp), so this is only the two rungs' dispatch --------------------

std::optional<Content> parse_content(std::string_view text, std::string* why, ContentProblem* what) {
  unsigned char ordinal = 0, problem = ROLLTUI_CONTENT_PROBLEM_NONE;
  int is_host = 0;
  const char *name = nullptr, *source = nullptr;
  std::size_t name_len = 0, source_len = 0;
  Str why_c;
  const int ok = rolltui_content_parse(text.data(), text.size(), &ordinal, &is_host, &name, &name_len, &source,
                                       &source_len, &problem, &why_c);
  if (what) *what = static_cast<ContentProblem>(problem);
  if (!ok) {
    if (why) *why = why_c.str();
    return std::nullopt;
  }
  Content c;
  if (is_host) {
    c.kind = WidgetKind::Registered;
    c.registered_name = std::string_view(name, name_len);
  } else {
    c.kind = static_cast<WidgetKind>(ordinal);
  }
  c.source = std::string_view(source, source_len);
  return c;
}

// The same two rungs as parse_content, in the same order, with the source carried
// through unjudged — see Layout.hpp for why a design tool needs that and a loader does not.
std::optional<Content> content_for_kind(std::string_view kind_name, std::string source) {
  Content c;
  c.source = source;
  unsigned char ordinal = 0;
  switch (rolltui_widget_kind_resolve(kind_name.data(), kind_name.size(), &ordinal, nullptr, nullptr, nullptr)) {
    case ROLLTUI_KIND_LIBRARY: c.kind = static_cast<WidgetKind>(ordinal); return c;
    case ROLLTUI_KIND_HOST:
      c.kind = WidgetKind::Registered;
      c.registered_name = kind_name;
      return c;
    default: return std::nullopt;
  }
}

std::string content_to_string(const Content& c) {
  const std::string_view kind_name = content_kind_name(c);
  Str out;
  rolltui_content_format(kind_name.data(), kind_name.size(), c.source.data(), c.source.size(),
                         static_cast<unsigned char>(content_source_rule(c)), &out);
  return out.str();
}

std::optional<std::string> migrated_content(std::string_view legacy) {
  std::size_t n = 0;
  if (const char* to = rolltui_migrated_content(legacy.data(), legacy.size(), &n)) return std::string(to, n);
  return std::nullopt;
}

// `Layout::popup()` is now inline on `RolltuiLayout` itself (rolltui_layout.h) — a linear
// scan needs nothing this file has that header does not already have.

// ---- names and text forms ----------------------------------------------------------------
// Anchor and border names are THIS module's own vocabulary (Layout.hpp states both closed
// lists), so — unlike Role — they are looked up straight through the C's tables.

std::string_view anchor_name(Anchor a) {
  std::size_t n = 0;
  const char* p = rolltui_anchor_name(static_cast<unsigned char>(a), &n);
  return {p, n};
}
std::optional<Anchor> anchor_from_name(std::string_view name) {
  unsigned char out = 0;
  if (rolltui_anchor_from_name(name.data(), name.size(), &out)) return static_cast<Anchor>(out);
  return std::nullopt;
}
std::string_view border_name(Border b) {
  std::size_t n = 0;
  const char* p = rolltui_border_name(static_cast<unsigned char>(b), &n);
  return {p, n};
}
std::optional<Border> border_from_name(std::string_view name) {
  unsigned char out = 0;
  if (rolltui_border_from_name(name.data(), name.size(), &out)) return static_cast<Border>(out);
  return std::nullopt;
}

// THE TEXT FORMS ARE THE BOUNDARY'S (Phase 15 m5), because the MENU needed them: a `size`
// or `dim` field checks a keystroke as a prefix of a valid value and canonicalises it, so
// the C menu widget has to parse and print a Dim too. These five are the C++ spelling over
// `rolltui/c/rolltui_layout.h`, and there is still one definition of what a dim looks like.
std::optional<Dim> parse_dim(std::string_view text) {
  Dim d;
  if (!rolltui_parse_dim(text.data(), text.size(), &d)) return std::nullopt;
  return d;
}

std::string dim_to_string(Dim d) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_dim_to_string(d, buf, sizeof buf));
}

std::optional<SplitSize> parse_split_size(std::string_view text) {
  SplitSize s;
  if (!rolltui_parse_split_size(text.data(), text.size(), &s)) return std::nullopt;
  return s;
}

std::optional<SplitSize> parse_size_text(std::string_view text) {
  SplitSize s;
  if (!rolltui_parse_size_text(text.data(), text.size(), &s)) return std::nullopt;
  return s;
}

std::string split_size_to_string(SplitSize s) {
  char buf[ROLLTUI_DIM_STRING_MAX];
  return std::string(buf, rolltui_split_size_to_string(s, buf, sizeof buf));
}

// ---- the loader ----------------------------------------------------------------------------
// `load_layout`, `layout_to_json[_value]`, `action_decl_problem` and every message a bad
// layout produces now live in `rolltui_layout.c` (Phase 17 m2); this section converts at the
// boundary `rolltui::Layout`/`rolltui::ActionDecl`'s own shape still needs (see the header
// comment) and otherwise adds nothing of its own.

std::string action_decl_problem(std::string_view name) {
  Str out;
  rolltui_action_decl_problem(name.data(), name.size(), &kHooks, &out);
  return out.str();
}

// The conversion `paint.cpp`/`studio.cpp` reach for where a `std::vector<ActionDecl>` is
// still what a caller (`Bindings::declare`, an `AppProfile::actions` built from a Layout's)
// needs — declared in Layout.hpp because it names `rolltui::ActionDecl`, which
// `rolltui_layout.h` must not (see that header's note on why `RolltuiLayoutAction` is named
// apart from it).
std::vector<ActionDecl> action_decls(const RolltuiActionList& actions) {
  std::vector<ActionDecl> out;
  out.reserve(actions.size());
  for (const RolltuiLayoutAction& a : actions) out.push_back({a.name.str(), a.description.str()});
  return out;
}

bool operator==(const RolltuiActionList& a, const std::vector<ActionDecl>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!(a[i].name == b[i].name) || !(a[i].description == b[i].description)) return false;
  return true;
}

namespace {

// `rolltui::ActionDecl` (Bindings.hpp) -> the loader's own `RolltuiLayoutAction` — needed
// only to hand `shipped_default_actions()`'s std::string-shaped table to the loader as its
// "no actions key at all" fallback. `Layout::actions` itself is `RolltuiActionList` already
// (Layout.hpp) and crosses with NO conversion at all, unlike before this port.
std::vector<RolltuiLayoutAction> actions_to_c(const std::vector<ActionDecl>& actions) {
  std::vector<RolltuiLayoutAction> out;
  out.reserve(actions.size());
  for (const ActionDecl& d : actions) out.push_back(RolltuiLayoutAction{Str(d.name), Str(d.description)});
  return out;
}

// Unpacks a filled `RolltuiLoadedLayout` into a fresh `Layout`, ONCE, right after a load —
// never retained past this call (the carrier's whole reason for being transient; see
// rolltui_layout.h). `base`/each popup are MOVED across (both already `RolltuiLayer`), not
// copied: the loaded tree is about to be released either way. Each action is a two-`RolltuiStr`
// COPY (`loaded` is released right after regardless; there is no move-from-array primitive
// worth adding for a handful of short strings read once per load).
Layout loaded_to_layout(RolltuiLoadedLayout& loaded) {
  Layout out;
  out.name = loaded.name;
  out.min_width = loaded.min_width;
  out.min_height = loaded.min_height;
  for (std::size_t i = 0; i < loaded.actions_n; ++i) out.actions.push_back(loaded.actions[i]);
  rolltui_layer_move(&out.base, &loaded.base);
  for (std::size_t i = 0; i < loaded.popups_n; ++i) out.popups.push_back(std::move(loaded.popups[i]));
  return out;
}

void report_from_c(const RolltuiLayoutReport& r, LayoutLoadReport& report) {
  report.error = r.error.str();
  for (std::size_t i = 0; i < r.unknown_keys_n; ++i) report.unknown_keys.emplace_back(r.unknown_keys[i].str());
  for (std::size_t i = 0; i < r.bad_values_n; ++i) report.bad_values.emplace_back(r.bad_values[i].str());
  for (std::size_t i = 0; i < r.migrated_n; ++i) report.migrated.emplace_back(r.migrated[i].str());
}

}  // namespace

std::optional<Layout> load_layout(std::string_view json_text, LayoutLoadReport& report) {
  const std::vector<RolltuiLayoutAction> default_actions = actions_to_c(shipped_default_actions());
  RolltuiLoadedLayout loaded;
  RolltuiLayoutReport rep{};
  rolltui_loaded_layout_init(&loaded);
  const int ok = rolltui_load_layout_text(json_text.data(), json_text.size(), &loaded, default_actions.data(),
                                          default_actions.size(), &kHooks, &rep);
  report_from_c(rep, report);
  rolltui_layout_report_release(&rep);
  if (!ok) {
    rolltui_loaded_layout_release(&loaded);
    return std::nullopt;
  }
  std::optional<Layout> out = loaded_to_layout(loaded);
  rolltui_loaded_layout_release(&loaded);
  return out;
}

std::optional<Layout> load_layout(const json::Value& root, LayoutLoadReport& report) {
  const std::vector<RolltuiLayoutAction> default_actions = actions_to_c(shipped_default_actions());
  RolltuiJsonValue* c_root = json::value_to_c(root);
  RolltuiLoadedLayout loaded;
  RolltuiLayoutReport rep{};
  rolltui_loaded_layout_init(&loaded);
  const int ok =
      rolltui_load_layout(c_root, &loaded, default_actions.data(), default_actions.size(), &kHooks, &rep);
  rolltui_json_free(c_root);
  report_from_c(rep, report);
  rolltui_layout_report_release(&rep);
  if (!ok) {
    rolltui_loaded_layout_release(&loaded);
    return std::nullopt;
  }
  std::optional<Layout> out = loaded_to_layout(loaded);
  rolltui_loaded_layout_release(&loaded);
  return out;
}

// `layout.actions`/`layout.popups` cross with NO conversion now — both are already the C
// shapes `rolltui_layout_to_json_value`/`_text` want (Layout.hpp), where this used to build
// a temporary `std::vector<RolltuiLayoutAction>` via `actions_to_c` on every dump.

json::Value layout_to_json_value(const Layout& layout) {
  RolltuiJsonValue* c =
      rolltui_layout_to_json_value(layout.name.data(), layout.name.size(), layout.min_width, layout.min_height,
                                   layout.actions.data(), layout.actions.size(), &layout.base, layout.popups.data(),
                                   layout.popups.size(), &kHooks);
  json::Value out = json::value_from_c(c);
  rolltui_json_free(c);
  return out;
}

std::string layout_to_json(const Layout& layout) {
  Str out;
  rolltui_layout_to_json_text(layout.name.data(), layout.name.size(), layout.min_width, layout.min_height,
                              layout.actions.data(), layout.actions.size(), &layout.base, layout.popups.data(),
                              layout.popups.size(), &kHooks, &out);
  return out.str();
}

// ---- built-ins -----------------------------------------------------------------------------

// The built-ins ARE the Layout domain's shipped presets (Phase 10 m1): real files under
// rolltui/presets/layouts/, embedded by cmake/embed_presets.cmake into the table below
// and parsed here. ONE definition site — before m1 the same four layouts existed twice,
// once as a string here and (as the plan wanted them) once as a file. Presets.cpp reads
// the same table for LayoutDomain::shipped_at, so a shipped preset and its built-in
// cannot drift; presets_test asserts they are equal anyway, because "cannot" has been
// wrong before.

namespace {

// Shipped order, the preset system's rule (PresetStore::shipped_names): "default"
// first — it is what a fresh install runs — then the table's own (alphabetical) order.
const std::vector<std::string_view>& builtin_names() {
  static const std::vector<std::string_view> names = [] {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i < rolltui_kLayoutPresetCount; ++i)
      if (std::string_view(rolltui_kLayoutPresets[i].name) == "default") out.push_back(rolltui_kLayoutPresets[i].name);
    for (std::size_t i = 0; i < rolltui_kLayoutPresetCount; ++i)
      if (std::string_view(rolltui_kLayoutPresets[i].name) != "default") out.push_back(rolltui_kLayoutPresets[i].name);
    return out;
  }();
  return names;
}

std::string_view builtin_json(std::string_view name) {
  for (std::size_t i = 0; i < rolltui_kLayoutPresetCount; ++i)
    if (std::string_view(rolltui_kLayoutPresets[i].name) == name) return rolltui_kLayoutPresets[i].text;
  return "";
}

}  // namespace

// Read straight out of the shipped "default" file's "actions" object — NEVER through
// load_layout, which asks for these when a file declares none and would recurse into
// itself. One definition site is still the file; this is a direct read of one key of it,
// now via `rolltui_layout_read_actions_key` (Phase 17 m2) rather than a local `json::Value`
// walk, since that primitive moved to the C alongside the rest of the loader.
const std::vector<ActionDecl>& shipped_default_actions() {
  // PHASE 17: parsing the shipped file's "actions" key and caching it is the library's
  // behaviour and lives in C (`rolltui_layout_shipped_default_actions`, which BORROWS into
  // storage released by `rolltui_shutdown`). This is the C++ view: the borrowed strings are
  // copied once into the owned-string shape `ActionDecl` has.
  static const std::vector<ActionDecl> decls = [] {
    std::vector<ActionDecl> out;
    std::size_t n = 0;
    const RolltuiLayoutAction* a = rolltui_layout_shipped_default_actions(&n);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.push_back({a[i].name.str(), a[i].description.str()});
    return out;
  }();
  return decls;
}

// The parsed built-in layouts. The biggest thing the library retains process-wide, and the
// reason `shutdown()` has real work to do today rather than only in principle: it is embedded
// JSON turned into Layout objects on first use and kept forever. Releasing it is safe at any
// moment — the next call rebuilds it (m6a).
std::vector<std::pair<std::string, Layout>> build_builtin_layouts() {
  {
    std::vector<std::pair<std::string, Layout>> out;
    for (std::string_view n : builtin_names()) {
      LayoutLoadReport rep;
      std::optional<Layout> l = load_layout(builtin_json(n), rep);
      if (!l || !rep.clean()) {
        // A built-in that does not load cleanly is a programming error; say so loudly
        // rather than serve a half-layout. (The test asserts clean() for each.)
        std::fprintf(stderr, "rolltui: built-in layout '%.*s' is broken: %s\n", static_cast<int>(n.size()), n.data(),
                     rep.error.empty() ? (rep.bad_values.empty() ? rep.unknown_keys[0].c_str() : rep.bad_values[0].c_str())
                                       : rep.error.c_str());
        std::abort();
      }
      out.emplace_back(std::string(n), std::move(*l));
    }
    return out;
  }
}

std::vector<std::pair<std::string, Layout>>& layout_cache_storage() {
  static std::vector<std::pair<std::string, Layout>> cache;
  return cache;
}

std::vector<std::pair<std::string, Layout>>& builtin_layout_cache() {
  // THE RELEASER TOUCHES THE STORAGE, NEVER THIS FUNCTION, and it is RE-REGISTERED ON EVERY
  // REBUILD. Two defects in one line, both from Phase 14 m6a and both invisible here
  // because a `std::vector<Layout>` reaches the global `operator new` and the gauge does
  // not count it; Theme.cpp's identical cache turned each of them into a failing assertion
  // the moment a theme owned a C effect map.
  //   - `builtin_layout_cache().clear(); builtin_layout_cache().shrink_to_fit();` — the
  //     second call finds the cache it just emptied and REBUILDS it, so shutdown ends
  //     holding what it meant to release.
  //   - `static const bool once = (on_shutdown(...), true)` registers once per PROCESS,
  //     while `shutdown()` clears its own registry as it runs — so a second shutdown
  //     releases nothing. Registering at FILL time is the rule `ThreadHandle` already
  //     states for a per-thread buffer.
  std::vector<std::pair<std::string, Layout>>& cache = layout_cache_storage();
  if (cache.empty())
    on_shutdown([] {
      layout_cache_storage().clear();
      layout_cache_storage().shrink_to_fit();
    });
  // FILLED WHEN EMPTY, not by a static initializer — because a `static x = f();` runs ONCE
  // and `shutdown()` clearing it would leave `builtin_layout()` answering nullptr forever
  // after. That was a live defect for about ten minutes, and it is exactly what "safe to call
  // at any time" has to mean: releasing a cache is only safe if the cache rebuilds.
  if (cache.empty()) cache = build_builtin_layouts();
  return cache;
}

const Layout* builtin_layout(std::string_view name) {
  for (const auto& [n, l] : builtin_layout_cache())
    if (n == name) return &l;
  return nullptr;
}

std::vector<std::string_view> builtin_layout_names() { return builtin_names(); }


// ---- the split, the drawing and the stack: over the boundary ---------------------------------

namespace {

// THE DRAW SCRATCH, owned per thread by the shim (rolltui/c/rolltui_frame_ops.h). One owner,
// named, released at thread exit and at `release_thread()` — `ThreadHandle` is the shape
// Phase 15 m2 extracted after `Unicode.cpp` hand-wrote it once.
RolltuiDrawScratch* draw_scratch() {
  static thread_local ThreadHandle<RolltuiDrawScratch, rolltui_draw_scratch_new, rolltui_draw_scratch_free> h;
  return h.get();
}

// …and the compose scratch, the same way. Two handles rather than one, because they are two
// ROLES: the arm maps are a frame's worth of bytes and the cluster array is a string's.
RolltuiComposeScratch* compose_scratch() {
  static thread_local ThreadHandle<RolltuiComposeScratch, rolltui_compose_scratch_new,
                                   rolltui_compose_scratch_free>
      h;
  return h.get();
}

// THE THREE ROLES A COMPOSE NEEDS, handed over as bytes. `rolltui/Style.hpp` is the one place
// these names exist; the C is told which byte to draw with, exactly as the markdown renderer
// and the diff colouriser are (Phase 15 m2's rule).
constexpr RolltuiLayoutRoles kRoles = {
    /*border=*/static_cast<unsigned char>(Role::border),
    /*border_active=*/static_cast<unsigned char>(Role::border_active),
    /*title=*/static_cast<unsigned char>(Role::title),
    /*overlay=*/static_cast<unsigned char>(Role::overlay),
};

// THE THREE STACK ACTIONS, likewise: the C knows the RULES and none of the words.
constexpr RolltuiStackActions kStackActions = {"stack.close_popup", "stack.focus_next", "stack.focus_prev"};

void push_node(void* ctx, const RolltuiResolvedNode* rn) {
  static_cast<std::vector<ResolvedNode>*>(ctx)->push_back(*rn);
}

// What crosses instead of a `std::function`: the host's callable behind a `void*`.
void call_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame*) {
  auto* p = static_cast<std::pair<const SlotRenderer*, Frame*>*>(ctx);
  (*p->first)(*rn, *p->second);
}

}  // namespace

void resolve_tree_into(const Node& root, Rect box, Rect screen, std::size_t layer,
                       std::vector<ResolvedNode>& out) {
  out.clear();
  rolltui_resolve_tree(&root, box, screen, layer, push_node, &out);
}

std::vector<ResolvedNode> resolve_tree(const Node& root, Rect box, Rect screen, std::size_t layer) {
  std::vector<ResolvedNode> out;
  resolve_tree_into(root, box, screen, layer, out);
  return out;
}

void draw_border(Frame& frame, Rect outer, Border b, const Style& line, std::string_view title,
                 const Style& title_style, bool ambiguous_wide) {
  rolltui_draw_border(frame.handle(), draw_scratch(), outer, static_cast<unsigned char>(b), line, title.data(),
                      title.size(), title_style, ambiguous_wide);
}

void compose_layer(Frame& frame, const std::vector<ResolvedNode>& nodes, const Theme& theme,
                   const SlotRenderer& render, bool ambiguous_wide) {
  std::pair<const SlotRenderer*, Frame*> ctx{&render, &frame};
  rolltui_compose_layer(frame.handle(), nodes.data(), nodes.size(), theme.styles.data(), &kRoles,
                        render ? call_slot : nullptr, &ctx, ambiguous_wide, compose_scratch());
}

// ---- the stack -------------------------------------------------------------------------------

WindowStack::WindowStack() = default;

WindowStack::WindowStack(const Layout& layout) { set_base(layout.base); }

void WindowStack::set_base(const Layer& base) { rolltui_window_stack_set_base(s_.get(), &base); }

void WindowStack::push(Layer popup) { rolltui_window_stack_push(s_.get(), &popup); }

bool WindowStack::pop() { return rolltui_window_stack_pop(s_.get()) != 0; }

bool WindowStack::has_popup(std::string_view id) const {
  return rolltui_window_stack_has_popup(s_.get(), id.data(), id.size()) != 0;
}

Node* WindowStack::find(std::string_view id) { return rolltui_window_stack_find(s_.get(), id.data(), id.size()); }

const Node* WindowStack::find(std::string_view id) const {
  return rolltui_window_stack_find(s_.get(), id.data(), id.size());
}

std::size_t WindowStack::focus_layer() const { return rolltui_window_stack_focus_layer(s_.get()); }

const Node* WindowStack::focused() const { return rolltui_window_stack_focused(s_.get()); }

void WindowStack::focus(std::string_view id) { rolltui_window_stack_focus(s_.get(), id.data(), id.size()); }

void WindowStack::cycle_focus(bool backwards) { rolltui_window_stack_cycle_focus(s_.get(), backwards); }

void WindowStack::resolve_into(Rect screen, std::vector<ResolvedNode>& out) const {
  out.clear();
  rolltui_window_stack_resolve(s_.get(), screen, push_node, &out);
}

std::vector<ResolvedNode> WindowStack::resolve(Rect screen) const {
  std::vector<ResolvedNode> out;
  resolve_into(screen, out);
  return out;
}

void WindowStack::compose(Frame& frame, Rect screen, const Theme& theme, const SlotRenderer& render,
                          bool ambiguous_wide) const {
  std::pair<const SlotRenderer*, Frame*> ctx{&render, &frame};
  rolltui_window_stack_compose(s_.get(), frame.handle(), screen, theme.styles.data(), &kRoles,
                               render ? call_slot : nullptr, &ctx, ambiguous_wide, compose_scratch());
}

Route WindowStack::route(const Event& e, Rect screen, const Bindings& bindings) {
  RolltuiEvent ev{};
  if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_KEY;
    ev.key = chord_of(*k);
  } else if (const MouseEvent* m = std::get_if<MouseEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_MOUSE;
    ev.mouse = *m;
  } else {
    ev.kind = ROLLTUI_EVENT_PASTE;
  }
  Str window;
  const unsigned char kind =
      rolltui_window_stack_route(s_.get(), &ev, screen, bindings.handle(), &kStackActions, &window);
  return {static_cast<Route::Kind>(kind), window.str()};
}

std::string_view WindowStack::captured() const {
  std::size_t n = 0;
  const char* p = rolltui_window_stack_captured(s_.get(), &n);
  return std::string_view(p, n);
}

}  // namespace rolltui
