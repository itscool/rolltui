// rolltui/Bindings.cpp — see Bindings.hpp.
#include "rolltui/Bindings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

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
      {"transcript.page_up", "scroll a page up"},
      {"transcript.page_down", "scroll a page down"},
      {"transcript.top", "scroll to the top"},
      {"transcript.bottom", "scroll to the bottom"},
      {"transcript.line_up", "scroll a line up"},
      {"transcript.line_down", "scroll a line down"},
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
      // The library's own tools (rolltui/tools/), which ship with it: the three editors
      // and the playground. `app.*` is deliberately NOT here — it is the APPLICATION's,
      // and every application is different, so a layout file declares it (milestone 4).
      {"editor.undo", "undo the last committed change"},
      {"editor.redo", "redo"},
      {"editor.theme", "open the theme editor"},
      {"editor.layout", "open the layout editor"},
      {"editor.keys", "open the keys editor"},
      {"playground.cycle_theme", "cycle the shipped theme presets"},
      {"playground.reload", "reload the fixture"},
      {"playground.quit", "quit"},
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

// ---- chords ---------------------------------------------------------------------------

namespace {

struct KeyName { const char* name; Key key; };
constexpr KeyName kKeyNames[] = {
    {"enter", Key::Enter}, {"tab", Key::Tab}, {"backspace", Key::Backspace}, {"escape", Key::Escape}, {"esc", Key::Escape},
    {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right}, {"home", Key::Home}, {"end", Key::End},
    {"pageup", Key::PageUp}, {"pagedown", Key::PageDown}, {"pgup", Key::PageUp}, {"pgdn", Key::PageDown}, {"insert", Key::Insert},
    {"delete", Key::Delete}, {"del", Key::Delete}, {"space", Key::Char},
    {"f1", Key::F1}, {"f2", Key::F2}, {"f3", Key::F3}, {"f4", Key::F4}, {"f5", Key::F5}, {"f6", Key::F6},
    {"f7", Key::F7}, {"f8", Key::F8}, {"f9", Key::F9}, {"f10", Key::F10}, {"f11", Key::F11}, {"f12", Key::F12},
};

std::string lower(std::string_view s) {
  std::string o(s);
  for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}

const char* key_name(Key k) {
  for (const KeyName& n : kKeyNames)
    if (n.key == k && std::string_view(n.name) != "esc" && std::string_view(n.name) != "pgup" && std::string_view(n.name) != "pgdn" &&
        std::string_view(n.name) != "del" && std::string_view(n.name) != "space")
      return n.name;
  return nullptr;
}

KeyEvent normalise(KeyEvent k) {
  k.raw.clear();
  return k;
}

}  // namespace

std::optional<KeyEvent> parse_chord(std::string_view text) {
  KeyEvent k;
  std::string t = lower(text);
  // Split on '+', but a trailing "+" alone is the plus character.
  std::vector<std::string> parts;
  std::string cur;
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '+' && !cur.empty() && i + 1 < t.size()) { parts.push_back(cur); cur.clear(); }
    else cur.push_back(t[i]);
  }
  if (cur.empty()) return std::nullopt;
  parts.push_back(cur);
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    const std::string& m = parts[i];
    if (m == "ctrl" || m == "control" || m == "c") k.ctrl = true;
    else if (m == "alt" || m == "meta" || m == "option" || m == "m") k.alt = true;
    else if (m == "shift" || m == "s") k.shift = true;
    else return std::nullopt;
  }
  const std::string& last = parts.back();
  for (const KeyName& n : kKeyNames)
    if (last == n.name) {
      k.key = n.key;
      if (last == "space") { k.key = Key::Char; k.ch = U' '; }
      return normalise(k);
    }
  const std::vector<unicode::DecodedChar> d = unicode::decode_utf8(last);
  if (d.size() != 1 || d[0].cp < 0x20) return std::nullopt;
  k.key = Key::Char;
  k.ch = d[0].cp;
  return normalise(k);
}

std::string chord_to_string(const KeyEvent& k) {
  if (k.key == Key::Unknown) return "";
  std::string s;
  if (k.ctrl) s += "ctrl+";
  if (k.alt) s += "alt+";
  if (k.shift) s += "shift+";
  if (k.key == Key::Char) {
    if (k.ch == U' ') s += "space";
    else unicode::append_utf8(s, k.ch);
  } else if (const char* n = key_name(k.key)) {
    s += n;
  }
  return s;
}

std::string chord_display(const KeyEvent& k) {
  std::string s = chord_to_string(k);
  // Capitalise each part, join with '-': "Ctrl-Shift-Left", "Alt-Enter", "F1", "?".
  std::string out;
  std::string part;
  auto flush = [&]() {
    if (part.empty()) return;
    if (!out.empty()) out += "-";
    if (part.size() > 1 || std::isalpha(static_cast<unsigned char>(part[0]))) part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
    if (part == "Pageup") part = "PgUp";
    if (part == "Pagedown") part = "PgDn";
    out += part;
    part.clear();
  };
  for (char c : s) {
    if (c == '+' && !part.empty()) flush();
    else part.push_back(c);
  }
  flush();
  return out;
}

// ---- the table ----------------------------------------------------------------------------

Bindings::Bindings() {
  for (const ActionInfo& a : library_actions()) {
    actions_.emplace_back(a.name);
    descriptions_.emplace_back(a.description);
    table_.emplace_back(std::string(a.name), std::vector<KeyEvent>{});
  }
}

void Bindings::add_action(std::string_view action, std::string_view description) {
  if (has(action)) return;
  actions_.emplace_back(action);
  descriptions_.emplace_back(description);
  // A row may already exist with chords in it — a bindings file loaded before the layout
  // declared this action kept them (Bindings.hpp). Declaring is what makes them live;
  // it must never throw them away.
  for (const auto& [a, c] : table_)
    if (a == action) return;
  table_.emplace_back(std::string(action), std::vector<KeyEvent>{});
}

// AUTHORITATIVE over every non-library scope, not merely additive (Phase 10 m6). The
// actions a screen emits are exactly what its layout declares, so a name the previous
// layout declared and this one does not is UNDECLARED again: its chord row survives
// untouched (a bindings file is global and the user's — Bindings.hpp's kept-and-inert
// rule), but action_for() stops answering with it and help stops listing it. Merely
// adding leaves the last screen's keys live under the next one — a key that works
// because of a layout you are no longer running, which is the implicit resolution this
// phase exists to remove. Clearing and re-adding also lets a hot-reloaded file change
// a description. The library's own scopes are closed and are never touched.
void Bindings::declare(const std::vector<ActionDecl>& declared) {
  for (std::size_t i = actions_.size(); i-- > 0;)
    if (!library_scope(scope_of(actions_[i]))) {
      actions_.erase(actions_.begin() + static_cast<std::ptrdiff_t>(i));
      descriptions_.erase(descriptions_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  for (const ActionDecl& d : declared) add_action(d.name, d.description);
}

std::vector<std::string> Bindings::undeclared() const {
  std::vector<std::string> out;
  for (const auto& [a, chords] : table_)
    if (!chords.empty() && !has(a)) out.push_back(a);
  return out;
}

std::string_view Bindings::description(std::string_view action) const {
  for (std::size_t i = 0; i < actions_.size(); ++i)
    if (actions_[i] == action) return descriptions_[i];
  return "";
}

bool Bindings::has(std::string_view action) const {
  return std::find(actions_.begin(), actions_.end(), action) != actions_.end();
}

std::vector<KeyEvent>& Bindings::chords_mut(std::string_view action) {
  for (auto& [a, c] : table_)
    if (a == action) return c;
  static std::vector<KeyEvent> none;
  none.clear();
  return none;
}

const std::vector<KeyEvent>& Bindings::chords_for(std::string_view action) const {
  for (const auto& [a, c] : table_)
    if (a == action) return c;
  static const std::vector<KeyEvent> none;
  return none;
}

std::string Bindings::chords_text(std::string_view action) const {
  std::string s;
  for (const KeyEvent& k : chords_for(action)) s += (s.empty() ? "" : ", ") + chord_display(k);
  return s;
}

std::string_view Bindings::action_for(const KeyEvent& key, std::string_view scope) const {
  const KeyEvent k = normalise(key);
  for (const auto& [a, chords] : table_) {
    if (scope_of(a) != scope) continue;
    if (!has(a)) continue;  // an undeclared row: kept, written back, never emitted
    for (const KeyEvent& c : chords)
      if (c == k) return a;
  }
  return "";
}

bool Bindings::bind(std::string_view action, const KeyEvent& chord_in, std::string* moved_from) {
  if (!has(action)) return false;
  const KeyEvent chord = normalise(chord_in);
  const bool is_enter = chord.key == Key::Enter && !chord.ctrl && !chord.alt && !chord.shift;
  if (is_enter && scope_of(action) == "input" && action != "input.submit") return false;  // the Enter rule
  const std::string_view scope = scope_of(action);
  for (auto& [a, chords] : table_) {
    if (a == action || scope_of(a) != scope) continue;
    auto it = std::find(chords.begin(), chords.end(), chord);
    if (it != chords.end()) {
      if (a == "input.submit" && is_enter) return false;  // never away from submit
      chords.erase(it);
      if (moved_from) *moved_from = a;
    }
  }
  std::vector<KeyEvent>& mine = chords_mut(action);
  if (std::find(mine.begin(), mine.end(), chord) == mine.end()) mine.push_back(chord);
  return true;
}

bool Bindings::unbind(std::string_view action, const KeyEvent& chord_in) {
  const KeyEvent chord = normalise(chord_in);
  if (action == "input.submit" && chord.key == Key::Enter && !chord.ctrl && !chord.alt && !chord.shift) return false;
  std::vector<KeyEvent>& mine = chords_mut(action);
  auto it = std::find(mine.begin(), mine.end(), chord);
  if (it == mine.end()) return false;
  mine.erase(it);
  return true;
}

void Bindings::clear(std::string_view action) {
  if (action == "input.submit") return;
  chords_mut(action).clear();
}

// ---- file format ----------------------------------------------------------------------------

std::string BindingsLoadReport::summary() const {
  if (clean()) return "";
  if (!error.empty()) return error;
  std::string s;
  auto add = [&](const std::string& x) { if (!s.empty()) s += "; "; s += x; };
  for (const std::string& x : bad_values) add("bad: " + x);
  for (const std::string& x : conflicts) add("conflict: " + x);
  for (const std::string& x : bad_chords) add("chord: " + x);
  for (const std::string& x : unknown_actions) add("unknown action: " + x);
  for (const std::string& x : unknown_keys) add("unknown: " + x);
  return s;
}

std::optional<Bindings> Bindings::from_json(std::string_view text, BindingsLoadReport& report) {
  report = BindingsLoadReport{};
  std::string err;
  json::Value v = json::parse(text, err);
  if (!err.empty()) { report.error = err; return std::nullopt; }
  return from_json(v, report);
}

std::optional<Bindings> Bindings::from_json(const json::Value& v, BindingsLoadReport& report) {
  report = BindingsLoadReport{};
  if (!v.is_object()) { report.error = "a bindings file must be a JSON object"; return std::nullopt; }
  const json::Value& map = v.get("bindings");
  if (!map.is_object()) { report.error = "a bindings file needs a \"bindings\" object"; return std::nullopt; }
  for (const auto& [k, x] : v.obj)
    if (k != "name" && k != "bindings" && k != "preset") report.unknown_keys.push_back(k);
  Bindings b;
  for (const auto& [action, chords] : map.obj) {
    if (!b.has(action)) {
      // A library scope is closed, so a name it does not define is a typo and is said
      // so. Any other scope belongs to a layout that this file knows nothing about: the
      // row is kept, inert, until something declares it (Bindings.hpp).
      if (library_scope(scope_of(action)) || scope_of(action) == action) {
        report.unknown_actions.push_back(action);
        continue;
      }
      b.table_.emplace_back(action, std::vector<KeyEvent>{});
    }
    if (!chords.is_array()) { report.bad_values.push_back(action + ": expected an array of chords"); continue; }
    for (const json::Value& c : chords.arr) {
      if (!c.is_string()) { report.bad_values.push_back(action + ": a chord must be a string"); continue; }
      const std::optional<KeyEvent> k = parse_chord(c.str);
      if (!k) { report.bad_chords.push_back(action + ": '" + c.str + "' is not a chord"); continue; }
      const bool is_enter = k->key == Key::Enter && !k->ctrl && !k->alt && !k->shift;
      if (is_enter && scope_of(action) == "input" && action != "input.submit") {
        report.bad_values.push_back(action + ": 'enter' is always input.submit and cannot be bound here (refused)");
        continue;
      }
      // A conflict within the scope: report it; the FIRST binding in the file wins.
      // Searched over the rows themselves, not through action_for, so two UNDECLARED
      // actions of one scope conflict here rather than silently once declared.
      std::string_view other;
      for (const auto& [a, cs] : b.table_) {
        if (scope_of(a) != scope_of(action)) continue;
        if (std::find(cs.begin(), cs.end(), *k) != cs.end()) { other = a; break; }
      }
      if (!other.empty() && other != action) {
        report.conflicts.push_back("'" + c.str + "' bound to both " + std::string(other) + " and " + action + " (" + std::string(other) + " kept)");
        continue;
      }
      std::vector<KeyEvent>& mine = b.chords_mut(action);
      if (std::find(mine.begin(), mine.end(), *k) == mine.end()) mine.push_back(*k);
    }
  }
  // The Enter rule, the other half: input.submit must have Enter.
  KeyEvent enter;
  enter.key = Key::Enter;
  const std::vector<KeyEvent>& submit = b.chords_for("input.submit");
  if (std::find(submit.begin(), submit.end(), enter) == submit.end()) {
    report.bad_values.push_back("input.submit: 'enter' is always bound to it (restored)");
    b.chords_mut("input.submit").push_back(enter);
  }
  return b;
}

json::Value Bindings::to_json(std::string_view name) const {
  json::Value root = json::Value::object();
  root.set("name", json::Value::string(std::string(name)));
  json::Value map = json::Value::object();
  for (const auto& [a, chords] : table_) {
    json::Value arr = json::Value::array();
    for (const KeyEvent& k : chords) arr.arr.push_back(json::Value::string(chord_to_string(k)));
    map.set(a, std::move(arr));
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

const Bindings& default_bindings() {
  static const Bindings b = [] {
    BindingsLoadReport rep;
    std::optional<Bindings> d = Bindings::from_json(default_bindings_json(), rep);
    if (!d || !rep.clean()) {
      std::fprintf(stderr, "rolltui: the shipped default bindings are broken: %s\n", rep.summary().c_str());
      std::abort();
    }
    // The shipped default LAYOUT declares the app scope (milestone 4). The two files
    // ship together, so this is the library's one complete "default screen + default
    // keys" — and a chord in the file for an app action the layout does not declare is
    // a build mistake, not a user's, so say so and stop rather than run half of one.
    // (Read straight out of the layout's own file; see shipped_default_actions.)
    d->declare(shipped_default_actions());
    if (const std::vector<std::string> dead = d->undeclared(); !dead.empty()) {
      std::fprintf(stderr, "rolltui: the shipped default bindings bind '%s', which no shipped layout declares\n", dead.front().c_str());
      std::abort();
    }
    return *d;
  }();
  return b;
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
