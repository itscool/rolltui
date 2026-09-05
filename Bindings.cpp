// rolltui/Bindings.cpp — see Bindings.hpp.
#include "rolltui/Bindings.hpp"
#include "rolltui/c/rolltui_embedded.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "rolltui/Lifetime.hpp"
#include "rolltui/Layout.hpp"  // shipped_default_actions() — the app scope the shipped screen declares
#include "rolltui/Unicode.hpp"

namespace rolltui {


// ---- the action table ------------------------------------------------------------------

const std::vector<ActionInfo>& library_actions() {
  // THE TABLE IS THE C's NOW (`c/rolltui_library_actions.c`), and this builds the C++ view
  // of it once. It moved because four consumers had copied all 59 rows verbatim when the
  // shim stopped being available — the vocabulary this rule meant to keep singular had
  // become quintuple. Built lazily and kept: the strings are static literals, so the
  // string_views below borrow rather than own.
  static const std::vector<ActionInfo> t = [] {
    std::vector<ActionInfo> v;
    const size_t n = rolltui_library_action_count();
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      size_t nl = 0, dl = 0;
      const char* nm = rolltui_library_action_name(i, &nl);
      const char* ds = rolltui_library_action_description(i, &dl);
      // string_view DIRECTLY onto the C's storage — never through a std::string. The
      // accessors borrow into static literals that outlive the process, which is exactly
      // what a string_view wants; constructing a std::string here would make a temporary,
      // take a view of it, and leave that view dangling at the end of the expression. I
      // wrote it that way first and eleven suites caught it.
      v.push_back({std::string_view(nm, nl), std::string_view(ds, dl)});
    }
    return v;
  }();
  return t;
}


std::string_view scope_of(std::string_view action) {
  const std::size_t dot = action.find('.');
  return dot == std::string_view::npos ? action : action.substr(0, dot);
}

bool library_scope(std::string_view scope) {
  // PHASE 17: the library's own closed scopes are a C fact now — one spelling of them.
  return rolltui_bindings_library_scope(nullptr, scope.data(), scope.size()) != 0;
}

// ---- renamed actions ------------------------------------------------------------------
//
// THE MIGRATION TABLE. Phase 11 m2 renamed `rolltui-playground` to `rolltui-studio`, and
// with it the three actions the tool declares. This is the one place in any source the
// old name survives — everything else that used to say it now says "studio", and a test
// greps for exactly that.
//
// It is a table of NAMES, not a rule about the word "playground": a bindings file is the
// user's, and a scope-prefix rewrite would also rename an action belonging to some other
// host's own tool that happens to share the prefix. Three rows, checked by name.
constexpr std::pair<const char*, const char*> kLegacyActions[] = {
    {"playground.cycle_theme", "studio.cycle_theme"},
    {"playground.reload", "studio.reload"},
    {"playground.quit", "studio.quit"},
};

std::optional<std::string> migrated_action(std::string_view legacy) {
  for (const auto& [from, to] : kLegacyActions)
    if (legacy == from) return std::string(to);
  return std::nullopt;
}

// ---- chords ---------------------------------------------------------------------------
//
// The SPELLING is behind the boundary (`rolltui/c/rolltui_bindings.h`) since Phase 15 m3:
// it is this module's own file format, which is the thing being ported. What is left here
// is the C++ shapes — `std::optional<KeyEvent>` and `std::string` — a caller already writes
// against, and the caller-sized buffer every string on that boundary is written into.

std::optional<KeyEvent> parse_chord(std::string_view text) {
  RolltuiChord c;
  if (!rolltui_chord_parse(text.data(), text.size(), &c)) return std::nullopt;
  return key_event_of(c);
}

std::string chord_to_string(const KeyEvent& k) {
  // CALLER-FILLED, with the bound known WITHOUT asking: three modifiers plus the longest
  // key name is stated as a constant in the header, so there is no measure-then-fill.
  char buf[ROLLTUI_CHORD_STRING_MAX];
  const RolltuiChord c = chord_of(k);
  return std::string(buf, rolltui_chord_to_string(&c, buf, sizeof buf));
}

std::string chord_display(const KeyEvent& k) {
  char buf[ROLLTUI_CHORD_STRING_MAX];
  const RolltuiChord c = chord_of(k);
  return std::string(buf, rolltui_chord_display(&c, buf, sizeof buf));
}

// ---- the table ----------------------------------------------------------------------------
//
// The STORAGE and every edit are behind the boundary; the SCOPE POLICY is here, because
// "which scopes are the library's" is a fact about `library_actions()` and the C is told it
// rather than deciding (rolltui/c/rolltui_bindings.h).

namespace {

// The predicate `undeclare_others` calls back into. Captureless, so it is a plain function
// pointer and the registration allocates nothing.
int is_library_scope(void*, const char* scope, std::size_t len) {
  return library_scope(std::string_view(scope, len)) ? 1 : 0;
}

// The Enter rule's SUBJECT — the one name the C is handed, once, so that the rule can live
// there and the vocabulary here (Bindings.hpp).
constexpr std::string_view kEnterAction = "input.submit";

// PHASE 17 m1: the file-format loader moved to C (rolltui_bindings_load_json); these two ask
// back the vocabulary questions that stay here, the same trade `is_library_scope` above
// already makes for "which scopes are the library's" — `migrated_action()`'s three-row table
// per the header comment there, and (Keys.cpp's own split, "the C classifies and never
// carries a sentence") the English for an undeliverable chord.
int migrate_cb(void*, const char* legacy, std::size_t len, char* out, std::size_t* out_len) {
  const std::optional<std::string> to = migrated_action(std::string_view(legacy, len));
  if (!to) return 0;
  const std::size_t n = std::min(to->size(), static_cast<std::size_t>(ROLLTUI_ACTION_NAME_MAX));
  std::memcpy(out, to->data(), n);
  *out_len = n;
  return 1;
}

std::size_t reason_cb(void*, const RolltuiChord* k, unsigned char protocol, char* out, std::size_t cap) {
  const std::string r = undeliverable_reason(key_event_of(*k), static_cast<KeyProtocol>(protocol));
  const std::size_t n = std::min(r.size(), cap);
  std::memcpy(out, r.data(), n);
  return n;
}

// The one place a `RolltuiBindingsReport`'s arrays become the `std::vector<std::string>`s
// every existing caller already writes against.
void copy_report(BindingsLoadReport& out, const RolltuiBindingsReport& in) {
  auto copy = [](const RolltuiStr* v, std::size_t n) {
    std::vector<std::string> r;
    r.reserve(n);
    for (std::size_t i = 0; i < n; ++i) r.emplace_back(v[i].view());
    return r;
  };
  out.error = in.error.str();
  out.unknown_actions = copy(in.unknown_actions, in.unknown_actions_n);
  out.bad_chords = copy(in.bad_chords, in.bad_chords_n);
  out.undeliverable = copy(in.undeliverable, in.undeliverable_n);
  out.conflicts = copy(in.conflicts, in.conflicts_n);
  out.bad_values = copy(in.bad_values, in.bad_values_n);
  out.unknown_keys = copy(in.unknown_keys, in.unknown_keys_n);
  out.migrated = copy(in.migrated, in.migrated_n);
}

}  // namespace

Bindings::Bindings() : b_(rolltui_bindings_new()) {
  rolltui_bindings_set_enter_rule(b_.get(), kEnterAction.data(), kEnterAction.size());
  for (const ActionInfo& a : library_actions())
    rolltui_bindings_add_action(b_.get(), a.name.data(), a.name.size(), a.description.data(), a.description.size());
}

void Bindings::add_action(std::string_view action, std::string_view description) {
  rolltui_bindings_add_action(b_.get(), action.data(), action.size(), description.data(), description.size());
}

// AUTHORITATIVE over every non-library scope, not merely additive (Phase 10 m6). The
// actions a screen emits are exactly what its layout declares plus what its host's
// mounted TOOLS bring (Phase 11 m1), so a name the previous
// layout declared and this one does not is UNDECLARED again: its chord row survives
// untouched (a bindings file is global and the user's — Bindings.hpp's kept-and-inert
// rule), but action_for() stops answering with it and help stops listing it. Merely
// adding leaves the last screen's keys live under the next one — a key that works
// because of a layout you are no longer running, which is the implicit resolution this
// phase exists to remove. Clearing and re-adding also lets a hot-reloaded file change
// a description. The library's own scopes are closed and are never touched.
void Bindings::declare(const std::vector<ActionDecl>& declared, const std::vector<ToolAction>& tools) {
  // PHASE 17: the composition — and above all its ORDER — is the library's behaviour and now
  // lives in C (`rolltui_bindings_declare`). This marshals and forwards; it decides nothing.
  std::vector<RolltuiLayoutAction> d(declared.size());
  for (std::size_t i = 0; i < declared.size(); ++i) {
    d[i] = RolltuiLayoutAction{};
    rolltui_str_set(&d[i].name, declared[i].name.data(), declared[i].name.size());
    rolltui_str_set(&d[i].description, declared[i].description.data(), declared[i].description.size());
  }
  // The C takes NUL-terminated strings; a string_view is not one, so the bytes are held for
  // the duration of the call and nothing outlives it.
  std::vector<std::string> keep;
  keep.reserve(tools.size() * 3);
  for (const ToolAction& x : tools) {
    keep.emplace_back(x.name);
    keep.emplace_back(x.description);
    keep.emplace_back(x.chord);
  }
  std::vector<RolltuiToolAction> ts(tools.size());
  for (std::size_t i = 0; i < tools.size(); ++i)
    ts[i] = RolltuiToolAction{keep[i * 3].c_str(), keep[i * 3 + 1].c_str(), keep[i * 3 + 2].c_str()};
  rolltui_bindings_declare(b_.get(), d.data(), d.size(), ts.data(), ts.size());
  for (RolltuiLayoutAction& a : d) {
    rolltui_str_free(&a.name);
    rolltui_str_free(&a.description);
  }
}

// A mounted tool's own defaults, filling GAPS only — see declare() in the header for the
// three ways a suggestion is declined, all of which leave the action present and unbound
// rather than absent or quietly sharing another action's chord.
void Bindings::suggest(const std::vector<ToolAction>& tools) {
  // PHASE 17: the gap-only rule is the library's and lives in C now. Marshal and forward.
  std::vector<std::string> keep;
  keep.reserve(tools.size() * 3);
  for (const ToolAction& x : tools) {
    keep.emplace_back(x.name);
    keep.emplace_back(x.description);
    keep.emplace_back(x.chord);
  }
  std::vector<RolltuiToolAction> ts(tools.size());
  for (std::size_t i = 0; i < tools.size(); ++i)
    ts[i] = RolltuiToolAction{keep[i * 3].c_str(), keep[i * 3 + 1].c_str(), keep[i * 3 + 2].c_str()};
  rolltui_bindings_suggest(b_.get(), ts.data(), ts.size());
}

// WHICH ROW OF A SCOPE ALREADY HOLDS THIS CHORD, or "". Over the ROWS themselves and not
// through action_for(), so an UNDECLARED row counts: two undeclared actions of one scope
// conflict at load rather than silently once something declares them.
std::string_view Bindings::holder(const KeyEvent& chord, std::string_view scope) const {
  // PHASE 17: the search is the library's (rolltui_bindings_holder). BORROWS into `b_`.
  const RolltuiChord want = chord_of(chord);
  std::size_t len = 0;
  const char* a = rolltui_bindings_holder(b_.get(), &want, scope.data(), scope.size(), &len);
  return a ? std::string_view(a, len) : std::string_view{};
}

std::vector<std::string> Bindings::undeclared() const {
  std::vector<std::string> out;
  const std::size_t rows = rolltui_bindings_row_count(b_.get());
  for (std::size_t i = 0; i < rows; ++i) {
    std::size_t alen = 0;
    const char* a = rolltui_bindings_row_at(b_.get(), i, &alen);
    if (rolltui_bindings_chord_count(b_.get(), a, alen) == 0) continue;
    if (rolltui_bindings_has(b_.get(), a, alen)) continue;
    out.emplace_back(a, alen);
  }
  return out;
}

std::string_view Bindings::description(std::string_view action) const {
  std::size_t len = 0;
  const char* p = rolltui_bindings_description(b_.get(), action.data(), action.size(), &len);
  return p ? std::string_view(p, len) : std::string_view();
}

bool Bindings::has(std::string_view action) const {
  return rolltui_bindings_has(b_.get(), action.data(), action.size()) != 0;
}

std::vector<std::string> Bindings::actions() const {
  std::vector<std::string> out;
  const std::size_t n = rolltui_bindings_action_count(b_.get());
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t len = 0;
    const char* p = rolltui_bindings_action_at(b_.get(), i, &len);
    out.emplace_back(p, len);
  }
  return out;
}

std::vector<KeyEvent> Bindings::chords_for(std::string_view action) const {
  const std::size_t n = rolltui_bindings_chord_count(b_.get(), action.data(), action.size());
  std::vector<KeyEvent> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord c;
    if (rolltui_bindings_chord_at(b_.get(), action.data(), action.size(), i, &c)) out.push_back(key_event_of(c));
  }
  return out;
}

// The help form skips what this terminal cannot deliver: a shortcut a menu or a help
// popup prints is a promise that the key works, and Phase 10 m6's rule ("inert also
// means invisible") applies to a chord the terminal cannot carry exactly as it does to
// an action nothing declares.
//
// Read through count-plus-index rather than through `chords_for`: this one IS on a draw
// path (a menu item's shortcut), so it allocates the string it returns and nothing else.
std::string Bindings::chords_text(std::string_view action) const {
  const unsigned char p = rolltui_key_active_protocol();
  const std::size_t n = rolltui_bindings_chord_count(b_.get(), action.data(), action.size());
  std::string s;
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord c;
    if (!rolltui_bindings_chord_at(b_.get(), action.data(), action.size(), i, &c)) continue;
    if (!rolltui_key_deliverable(&c, p)) continue;
    char buf[ROLLTUI_CHORD_STRING_MAX];
    const std::size_t len = rolltui_chord_display(&c, buf, sizeof buf);
    if (!s.empty()) s += ", ";
    s.append(buf, len);
  }
  return s;
}

std::string_view Bindings::action_for(const KeyEvent& key, std::string_view scope) const {
  const RolltuiChord k = chord_of(key);
  std::size_t len = 0;
  const char* a = rolltui_bindings_action_for(b_.get(), &k, scope.data(), scope.size(), &len);
  return a ? std::string_view(a, len) : std::string_view();
}

bool Bindings::bind(std::string_view action, const KeyEvent& chord_in, std::string* moved_from) {
  const RolltuiChord chord = chord_of(chord_in);
  const char* moved = nullptr;
  std::size_t moved_len = 0;
  if (!rolltui_bindings_bind(b_.get(), action.data(), action.size(), &chord, &moved, &moved_len)) return false;
  if (moved && moved_from) moved_from->assign(moved, moved_len);
  return true;
}

bool Bindings::unbind(std::string_view action, const KeyEvent& chord_in) {
  const RolltuiChord chord = chord_of(chord_in);
  return rolltui_bindings_unbind(b_.get(), action.data(), action.size(), &chord) != 0;
}

void Bindings::clear(std::string_view action) { rolltui_bindings_clear(b_.get(), action.data(), action.size()); }

// ---- file format ----------------------------------------------------------------------------

std::string BindingsLoadReport::summary() const {
  if (clean()) return "";
  if (!error.empty()) return error;
  std::string s;
  auto add = [&](const std::string& x) { if (!s.empty()) s += "; "; s += x; };
  for (const std::string& x : bad_values) add("bad: " + x);
  for (const std::string& x : conflicts) add("conflict: " + x);
  for (const std::string& x : bad_chords) add("chord: " + x);
  for (const std::string& x : undeliverable) add("undeliverable: " + x);
  for (const std::string& x : unknown_actions) add("unknown action: " + x);
  for (const std::string& x : unknown_keys) add("unknown: " + x);
  return s;
}

std::optional<Bindings> Bindings::from_json(std::string_view text, BindingsLoadReport& report) {
  return from_json(text, report, active_key_protocol());
}

std::optional<Bindings> Bindings::from_json(const json::Value& v, BindingsLoadReport& report) {
  return from_json(v, report, active_key_protocol());
}

// PHASE 17 m1: the loader itself — the walk over the file's JSON, chord by chord, and every
// rule from the header comment (kept-and-inert, the rename migration, the deliverability
// refusal, the Enter rule, a scope conflict) — moved to C (`rolltui_bindings_load_json`) now
// that `rolltui_json.h` exists to build it on. What is left here is what stayed at Phase 15
// m3 for the same reason and never crossed: `library_scope`/`migrated_action` (the action
// vocabulary) and `undeliverable_reason` (Keys.cpp's own English), asked back through the
// three callbacks above.
std::optional<Bindings> Bindings::from_json(std::string_view text, BindingsLoadReport& report, KeyProtocol deliver) {
  Bindings b;  // seeded with library_actions() — the loader ADDS the file's rows onto it
  RolltuiBindingsReport rep{};
  const int ok = rolltui_bindings_load_json(b.b_.get(), text.data(), text.size(), static_cast<unsigned char>(deliver),
                                            is_library_scope, nullptr, migrate_cb, nullptr, reason_cb, nullptr, &rep);
  copy_report(report, rep);
  rolltui_bindings_report_release(&rep);
  if (!ok) return std::nullopt;
  return b;
}

// The Value overload exists for a caller that already holds a parsed file — Presets.cpp's
// `BindingsDomain::parse`, which `PresetStore`'s own generic text→Value conversion drives —
// and re-dumps it to text rather than duplicating the walk above: the "thin shim: convert in,
// call this file, convert out" `rolltui_json.h`'s header comment describes for exactly this
// situation. A preset loads once, not per frame, so the extra pass is not one this library's
// budget covers.
std::optional<Bindings> Bindings::from_json(const json::Value& v, BindingsLoadReport& report, KeyProtocol deliver) {
  return from_json(json::dump(v, 0), report, deliver);
}

json::Value Bindings::to_json(std::string_view name) const {
  RolltuiStr text;
  rolltui_bindings_dump_json(b_.get(), name.data(), name.size(), &text);
  std::string err;
  return json::parse(text.view(), err);
}

// ---- the shipped default ------------------------------------------------------------------------

std::string_view default_bindings_json() {
  for (std::size_t i = 0; i < rolltui_kBindingsPresetCount; ++i)
    if (std::string_view(rolltui_kBindingsPresets[i].name) == "default") return rolltui_kBindingsPresets[i].text;
  return "";
}

// A CACHE WITH A RELEASER, not a `static const Bindings` — changed in Phase 15 m3 for the
// same reason `builtin_theme_cache()` was: a `Bindings` owns a C table now, which is an
// explicit allocation through the library's own entry point, so one held for the life of
// the process is a PROCESS-WIDE RETAINER and `rolltui::shutdown()` promises `live_bytes ==
// 0`. Before the port the table was `std::vector`s reaching the global `operator new`,
// which the gauge cannot see, and the promise was quietly weaker.
//
// Filled when empty, and the releaser RE-REGISTERED on every rebuild — `shutdown()` clears
// its own registry as it runs, so a `static bool once` would release this the first time
// and never again.
std::optional<Bindings>& default_bindings_cache() {
  static std::optional<Bindings> cache;
  return cache;
}

const Bindings& default_bindings() {
  // PHASE 17: the shipped table — parsing it, validating it against the WEAKEST protocol, and
  // both build-stopping aborts — is the library's behaviour and lives in C
  // (`rolltui_bindings_default`). What stays here is the C++ VIEW of it: the C holds the one
  // table and hands out a borrow, so this owns a clone. The cache and its shutdown hook are
  // kept exactly as they were, because `rolltui_shutdown` asserts `live_bytes == 0` and this
  // clone is live bytes.
  std::optional<Bindings>& cache = default_bindings_cache();
  if (cache) return *cache;
  on_shutdown([] { default_bindings_cache().reset(); });
  cache = Bindings::adopt(rolltui_bindings_clone(rolltui_bindings_default()));
  return *cache;
}

std::vector<std::string> help_lines(const Bindings& b, std::string_view scope, const std::vector<std::string>& actions) {
  std::vector<std::string> out;
  std::vector<std::string> list = actions;
  if (list.empty())
    for (const std::string& a : b.actions())
      if (scope_of(a) == scope) list.push_back(a);
  // The chord column is padded to the widest chord, capped at 24 cells so one long
  // chord does not push every description off a narrow popup; a longer chord is simply
  // followed by two spaces.
  std::size_t width = 0;
  std::vector<std::string> chords;
  for (const std::string& a : list) {
    chords.push_back(b.chords_text(a));
    width = std::max(width, std::min<std::size_t>(chords.back().size(), 22));
  }
  const std::size_t column = std::max<std::size_t>(width + 2, 12);
  for (std::size_t i = 0; i < list.size(); ++i) {
    std::string line = chords[i].empty() ? "(unbound)" : chords[i];
    if (line.size() + 2 <= column) line.append(column - line.size(), ' ');
    else line += "  ";
    line += b.description(list[i]);
    out.push_back(line);
  }
  return out;
}

}  // namespace rolltui
