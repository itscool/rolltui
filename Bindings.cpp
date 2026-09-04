// rolltui/Bindings.cpp — see Bindings.hpp.
#include "rolltui/Bindings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "rolltui/Lifetime.hpp"
#include "rolltui/Layout.hpp"  // shipped_default_actions() — the app scope the shipped screen declares
#include "rolltui/Unicode.hpp"

namespace rolltui {

namespace embedded {
extern const std::pair<std::string_view, std::string_view> kBindingsPresets[];
extern const std::size_t kBindingsPresetCount;
}  // namespace embedded

// ---- the action table ------------------------------------------------------------------

const std::vector<ActionInfo>& library_actions() {
  static const std::vector<ActionInfo> t = {
      {"input.submit", "send the line (always Enter)"},
      {"input.newline", "insert a newline"},
      {"input.backspace", "erase before the caret (or the selection)"},
      {"input.delete", "erase after the caret (or the selection)"},
      {"input.kill_word_backward", "kill the word before the caret"},
      {"input.kill_word_forward", "kill the word after the caret"},
      {"input.kill_to_line_start", "kill to the start of the line"},
      {"input.kill_to_line_end", "kill to the end of the line"},
      {"input.left", "move one grapheme left"},
      {"input.right", "move one grapheme right"},
      {"input.word_left", "move one word left"},
      {"input.word_right", "move one word right"},
      {"input.line_start", "start of the line (scrolls when empty)"},
      {"input.line_end", "end of the line (scrolls when empty)"},
      {"input.up", "up a row, or the previous history entry"},
      {"input.down", "down a row, or the next history entry"},
      {"input.select_left", "extend the selection one grapheme left"},
      {"input.select_right", "extend the selection one grapheme right"},
      {"input.select_word_left", "extend the selection one word left"},
      {"input.select_word_right", "extend the selection one word right"},
      {"input.select_line_start", "extend the selection to the start of the line"},
      {"input.select_line_end", "extend the selection to the end of the line"},
      {"input.select_up", "extend the selection up a row"},
      {"input.select_down", "extend the selection down a row"},
      {"input.select_all", "select all"},
      {"input.clear_selection", "clear the selection"},
      {"input.copy", "copy the selection"},
      {"input.eof", "end of input on an empty line, else delete"},
      {"input.undo", "undo the last group of edits"},
      {"input.redo", "redo"},
      {"transcript.page_up", "scroll a page up"},
      {"transcript.page_down", "scroll a page down"},
      {"transcript.top", "scroll to the top"},
      {"transcript.bottom", "scroll to the bottom"},
      {"transcript.line_up", "scroll a line up"},
      {"transcript.line_down", "scroll a line down"},
      {"transcript.find_next", "go to the next match"},
      {"transcript.find_prev", "go to the previous match"},
      {"transcript.fold", "toggle the first folded block in view"},
      {"transcript.copy", "copy the selection again"},
      {"transcript.clear_selection", "clear the selection"},
      {"menu.up", "previous item"},
      {"menu.down", "next item"},
      {"menu.page_up", "a page of items up"},
      {"menu.page_down", "a page of items down"},
      {"menu.first", "the first item"},
      {"menu.last", "the last item"},
      {"menu.activate", "act on the item"},
      {"menu.descend", "descend into a submenu or choice"},
      {"menu.ascend", "up one level"},
      {"menu.back", "clear the filter / up a level / close"},
      {"menu.erase", "erase the last filter character"},
      {"edit.commit", "commit the value being edited"},
      {"edit.cancel", "cancel the edit (the value returns)"},
      {"edit.step_up", "a number field: step up"},
      {"edit.step_down", "a number field: step down"},
      {"stack.close_popup", "close the topmost popup"},
      {"stack.focus_next", "move focus to the next window"},
      {"stack.focus_prev", "move focus to the previous window"},
      // Nothing follows the WIDGET scopes. `app.*` left this table in Phase 10 m4 (it is
      // the APPLICATION's, and a layout file declares it); `editor.*` and the studio's
      // followed it in Phase 11 m1 (they are the library's own TOOLS', and whoever mounts
      // a tool declares them — rolltui/tools/tool_actions.hpp). Adding a scope here means
      // claiming EVERY host performs it.
  };
  return t;
}


std::string_view scope_of(std::string_view action) {
  const std::size_t dot = action.find('.');
  return dot == std::string_view::npos ? action : action.substr(0, dot);
}

bool library_scope(std::string_view scope) {
  for (const ActionInfo& a : library_actions())
    if (scope_of(a.name) == scope) return true;
  return false;
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
  // The suggestions FIRST, and this order is the whole reason the two are one call: a
  // declaration creates an empty row for its action (add_action), so a suggestion made
  // afterwards would see that row and decline every time — a tool whose keys are all
  // silently unbound, which is exactly what the first cut of this did.
  suggest(tools);
  rolltui_bindings_undeclare_others(b_.get(), is_library_scope, nullptr);
  for (const ActionDecl& d : declared) add_action(d.name, d.description);
  for (const ToolAction& t : tools) add_action(t.name, t.description);
}

// A mounted tool's own defaults, filling GAPS only — see declare() in the header for the
// three ways a suggestion is declined, all of which leave the action present and unbound
// rather than absent or quietly sharing another action's chord.
void Bindings::suggest(const std::vector<ToolAction>& tools) {
  for (const ToolAction& t : tools) {
    if (rolltui_bindings_has_row(b_.get(), t.name.data(), t.name.size())) continue;
    const std::optional<KeyEvent> k = parse_chord(t.chord);
    const bool taken = k && !holder(*k, scope_of(t.name)).empty();
    rolltui_bindings_add_row(b_.get(), t.name.data(), t.name.size());
    if (k && !taken) {
      const RolltuiChord c = chord_of(*k);
      rolltui_bindings_add_chord(b_.get(), t.name.data(), t.name.size(), &c);
    }
  }
}

// WHICH ROW OF A SCOPE ALREADY HOLDS THIS CHORD, or "". Over the ROWS themselves and not
// through action_for(), so an UNDECLARED row counts: two undeclared actions of one scope
// conflict at load rather than silently once something declares them.
std::string_view Bindings::holder(const KeyEvent& chord, std::string_view scope) const {
  const RolltuiChord want = chord_of(chord);
  const std::size_t rows = rolltui_bindings_row_count(b_.get());
  for (std::size_t i = 0; i < rows; ++i) {
    std::size_t alen = 0;
    const char* a = rolltui_bindings_row_at(b_.get(), i, &alen);
    if (scope_of(std::string_view(a, alen)) != scope) continue;
    const std::size_t n = rolltui_bindings_chord_count(b_.get(), a, alen);
    for (std::size_t j = 0; j < n; ++j) {
      RolltuiChord c;
      if (rolltui_bindings_chord_at(b_.get(), a, alen, j, &c) && key_event_of(c) == key_event_of(want))
        return std::string_view(a, alen);
    }
  }
  return {};
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

std::optional<Bindings> Bindings::from_json(std::string_view text, BindingsLoadReport& report, KeyProtocol deliver) {
  report = BindingsLoadReport{};
  std::string err;
  json::Value v = json::parse(text, err);
  if (!err.empty()) { report.error = err; return std::nullopt; }
  return from_json(v, report, deliver);
}

std::optional<Bindings> Bindings::from_json(const json::Value& v, BindingsLoadReport& report, KeyProtocol deliver) {
  report = BindingsLoadReport{};
  if (!v.is_object()) { report.error = "a bindings file must be a JSON object"; return std::nullopt; }
  const json::Value& map = v.get("bindings");
  if (!map.is_object()) { report.error = "a bindings file needs a \"bindings\" object"; return std::nullopt; }
  for (const auto& [k, x] : v.obj)
    if (k != "name" && k != "bindings" && k != "preset") report.unknown_keys.push_back(k);
  Bindings b;
  for (const auto& [key, chords] : map.obj) {
    // A RENAMED action is rewritten once, here, before anything else reads the name —
    // and it has to be before, because the kept-and-inert rule two lines down would
    // otherwise file `playground.quit` as some other screen's row and Ctrl-Q would
    // quietly stop quitting. Said in `migrated`, never a problem (Bindings.hpp).
    std::string action = key;
    if (std::optional<std::string> to = migrated_action(action)) {
      report.migrated.push_back("'" + action + "' \xE2\x86\x92 '" + *to + "'");
      action = *to;
    }
    if (!b.has(action)) {
      // A library scope is closed, so a name it does not define is a typo and is said
      // so. Any other scope belongs to a layout that this file knows nothing about: the
      // row is kept, inert, until something declares it (Bindings.hpp).
      if (library_scope(scope_of(action)) || scope_of(action) == action) {
        report.unknown_actions.push_back(action);
        continue;
      }
      // Only if there is no row yet. A JSON object cannot repeat a key, but a MIGRATED
      // name can land on one the file already wrote ("studio.quit": [] beside a
      // "playground.quit"), and two rows for one action would leave chords_mut() filling
      // the first while lookup answered from whichever came first — the chords the user
      // can see and the chords that fire, in two different places.
      rolltui_bindings_add_row(b.b_.get(), action.data(), action.size());
    }
    if (!chords.is_array()) { report.bad_values.push_back(action + ": expected an array of chords"); continue; }
    for (const json::Value& c : chords.arr) {
      if (!c.is_string()) { report.bad_values.push_back(action + ": a chord must be a string"); continue; }
      const std::optional<KeyEvent> k = parse_chord(c.str);
      if (!k) { report.bad_chords.push_back(action + ": '" + c.str + "' is not a chord"); continue; }
      // A chord this terminal cannot deliver: named, with the reason and what to turn on
      // — and then KEPT anyway (no `continue`). The refusal is about what can fire, not
      // about what the user is allowed to have written: the row round-trips through save,
      // so the same file loads clean the day the terminal negotiates kitty, and
      // action_for()/chords_text() are what make it inert and invisible until then.
      if (!deliverable(*k, deliver))
        report.undeliverable.push_back(action + ": '" + chord_to_string(*k) + "' cannot be delivered by this terminal; " +
                                       undeliverable_reason(*k, deliver));
      const bool is_enter = k->key == Key::Enter && !k->ctrl && !k->alt && !k->shift;
      if (is_enter && scope_of(action) == "input" && action != "input.submit") {
        report.bad_values.push_back(action + ": 'enter' is always input.submit and cannot be bound here (refused)");
        continue;
      }
      // A conflict within the scope: report it; the FIRST binding in the file wins.
      // Searched over the rows themselves, not through action_for, so two UNDECLARED
      // actions of one scope conflict here rather than silently once declared.
      const std::string_view other = b.holder(*k, scope_of(action));
      if (!other.empty() && other != action) {
        report.conflicts.push_back("'" + c.str + "' bound to both " + std::string(other) + " and " + action + " (" + std::string(other) + " kept)");
        continue;
      }
      const RolltuiChord c0 = chord_of(*k);
      rolltui_bindings_add_chord(b.b_.get(), action.data(), action.size(), &c0);
    }
  }
  // The Enter rule, the other half: input.submit must have Enter.
  KeyEvent enter;
  enter.key = Key::Enter;
  const std::vector<KeyEvent> submit = b.chords_for(kEnterAction);
  if (std::find(submit.begin(), submit.end(), enter) == submit.end()) {
    report.bad_values.push_back("input.submit: 'enter' is always bound to it (restored)");
    const RolltuiChord c0 = chord_of(enter);
    rolltui_bindings_add_chord(b.b_.get(), kEnterAction.data(), kEnterAction.size(), &c0);
  }
  return b;
}

json::Value Bindings::to_json(std::string_view name) const {
  json::Value root = json::Value::object();
  root.set("name", json::Value::string(std::string(name)));
  json::Value map = json::Value::object();
  const std::size_t rows = rolltui_bindings_row_count(b_.get());
  for (std::size_t i = 0; i < rows; ++i) {
    std::size_t alen = 0;
    const char* a = rolltui_bindings_row_at(b_.get(), i, &alen);
    json::Value arr = json::Value::array();
    for (const KeyEvent& k : chords_for(std::string_view(a, alen)))
      arr.arr.push_back(json::Value::string(chord_to_string(k)));
    map.set(std::string(a, alen), std::move(arr));
  }
  root.set("bindings", std::move(map));
  return root;
}

// ---- the shipped default ------------------------------------------------------------------------

std::string_view default_bindings_json() {
  for (std::size_t i = 0; i < embedded::kBindingsPresetCount; ++i)
    if (embedded::kBindingsPresets[i].first == "default") return embedded::kBindingsPresets[i].second;
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
  std::optional<Bindings>& cache = default_bindings_cache();
  if (cache) return *cache;
  on_shutdown([] { default_bindings_cache().reset(); });
  cache = [] {
    BindingsLoadReport rep;
    // AGAINST LEGACY, EXPLICITLY, and not against whatever this terminal turned out to
    // be (Phase 12 m3). The shipped file belongs to every host on every terminal, so it
    // must be deliverable under the WEAKEST model — proved here, once, for every build,
    // exactly as the tool-row abort below is. A chord that only works on kitty is a fine
    // thing for a user's own file and a build mistake in this one; checking it against
    // the active protocol would let a kitty terminal ship a file a plain xterm cannot
    // press, which is the same defect one level up.
    std::optional<Bindings> d = Bindings::from_json(default_bindings_json(), rep, KeyProtocol::Legacy);
    if (!d || !rep.clean()) {
      std::fprintf(stderr, "rolltui: the shipped default bindings are broken: %s\n", rep.summary().c_str());
      std::abort();
    }
    // The shipped default LAYOUT declares the app scope (milestone 4). The two files
    // ship together, so this is the library's one complete "default screen + default
    // keys" — and a chord in the file for an app action the layout does not declare is
    // a build mistake, not a user's, so say so and stop rather than run half of one.
    // (Read straight out of the layout's own file; see shipped_default_actions.)
    //
    // Phase 11 m1 gave this abort a second job, which is why it is worth more than the
    // three lines it costs: the shipped file belongs to EVERY host, so it may bind the
    // library's widgets and the shipped screen's own actions and NOTHING ELSE. A row
    // for `studio.quit` here would be a key every host that never mounts the studio
    // advertises and cannot press — the defect m1 removed, re-created in
    // file form. A mounted tool's chords come from the tool (Bindings::suggest), so a
    // tool row in this file now stops the build instead of shipping.
    d->declare(shipped_default_actions());
    if (const std::vector<std::string> dead = d->undeclared(); !dead.empty()) {
      std::fprintf(stderr, "rolltui: the shipped default bindings bind '%s', which no shipped layout declares (a mounted tool's chords belong to the tool)\n",
                   dead.front().c_str());
      std::abort();
    }
    return *d;
  }();
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
