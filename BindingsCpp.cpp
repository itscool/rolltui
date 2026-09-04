// rolltui/BindingsCpp.cpp — the C++ side of chords and the binding table, behind the same
// boundary as `c/rolltui_bindings.c` (Phase 15 m3). One of the two links; the flag
// `-DROLLTUI_C` picks which. See rolltui_bindings.h for the boundary's rules and
// rolltui/Bindings.hpp for the binding rules themselves.
//
// This is the shape the module has always had — `std::string` for a name, a
// `std::vector<std::pair<std::string, std::vector<KeyEvent>>>` for the table — kept
// deliberately, so the two implementations differ in the way the languages do and not
// because one of them was rewritten while it was being moved.
#include "rolltui/c/rolltui_bindings.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Unicode.hpp"

namespace {

struct KeyName {
  const char* name;
  unsigned char key;
  bool canonical;  // false for an alias: parsed, never printed
};

// THIS MODULE'S OWN FILE FORMAT, which is why it is here and `library_actions()` is not.
constexpr KeyName kKeyNames[] = {
    {"enter", ROLLTUI_KEY_ENTER, true},       {"tab", ROLLTUI_KEY_TAB, true},
    {"backspace", ROLLTUI_KEY_BACKSPACE, true}, {"escape", ROLLTUI_KEY_ESCAPE, true},
    {"esc", ROLLTUI_KEY_ESCAPE, false},       {"up", ROLLTUI_KEY_UP, true},
    {"down", ROLLTUI_KEY_DOWN, true},         {"left", ROLLTUI_KEY_LEFT, true},
    {"right", ROLLTUI_KEY_RIGHT, true},       {"home", ROLLTUI_KEY_HOME, true},
    {"end", ROLLTUI_KEY_END, true},           {"pageup", ROLLTUI_KEY_PAGEUP, true},
    {"pagedown", ROLLTUI_KEY_PAGEDOWN, true}, {"pgup", ROLLTUI_KEY_PAGEUP, false},
    {"pgdn", ROLLTUI_KEY_PAGEDOWN, false},    {"insert", ROLLTUI_KEY_INSERT, true},
    {"delete", ROLLTUI_KEY_DELETE, true},     {"del", ROLLTUI_KEY_DELETE, false},
    {"space", ROLLTUI_KEY_CHAR, false},       {"f1", ROLLTUI_KEY_F1, true},
    {"f2", ROLLTUI_KEY_F1 + 1, true},         {"f3", ROLLTUI_KEY_F1 + 2, true},
    {"f4", ROLLTUI_KEY_F1 + 3, true},         {"f5", ROLLTUI_KEY_F1 + 4, true},
    {"f6", ROLLTUI_KEY_F1 + 5, true},         {"f7", ROLLTUI_KEY_F1 + 6, true},
    {"f8", ROLLTUI_KEY_F1 + 7, true},         {"f9", ROLLTUI_KEY_F1 + 8, true},
    {"f10", ROLLTUI_KEY_F1 + 9, true},        {"f11", ROLLTUI_KEY_F1 + 10, true},
    {"f12", ROLLTUI_KEY_F12, true},
};

std::string lower(std::string_view s) {
  std::string o(s);
  for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}

const char* canonical_key_name(unsigned char k) {
  for (const KeyName& n : kKeyNames)
    if (n.key == k && n.canonical) return n.name;
  return nullptr;
}

bool chord_eq(const RolltuiChord& a, const RolltuiChord& b) {
  return a.key == b.key && a.ch == b.ch && a.ctrl == b.ctrl && a.alt == b.alt && a.shift == b.shift;
}

std::string_view scope_view(std::string_view action) {
  const std::size_t dot = action.find('.');
  return dot == std::string_view::npos ? action : action.substr(0, dot);
}

bool is_bare_enter(const RolltuiChord& k) {
  return k.key == ROLLTUI_KEY_ENTER && !k.ctrl && !k.alt && !k.shift;
}

std::string chord_string(const RolltuiChord& k) {
  if (k.key == ROLLTUI_KEY_UNKNOWN) return "";
  std::string s;
  if (k.ctrl) s += "ctrl+";
  if (k.alt) s += "alt+";
  if (k.shift) s += "shift+";
  if (k.key == ROLLTUI_KEY_CHAR) {
    if (k.ch == U' ') s += "space";
    else rolltui::unicode::append_utf8(s, k.ch);
  } else if (const char* n = canonical_key_name(k.key)) {
    s += n;
  }
  return s;
}

}  // namespace

// ---- the table ---------------------------------------------------------------------------
// THREE PARALLEL LISTS, exactly as the header describes: the declared actions with their
// descriptions, and the rows. A row may outlive a declaration (the kept-and-inert rule).
struct RolltuiBindings {
  std::vector<std::string> actions;
  std::vector<std::string> descriptions;
  std::vector<std::pair<std::string, std::vector<RolltuiChord>>> rows;
  std::string enter_action;

  std::vector<RolltuiChord>* find(std::string_view action) {
    for (auto& [a, c] : rows)
      if (a == action) return &c;
    return nullptr;
  }
  const std::vector<RolltuiChord>* find(std::string_view action) const {
    for (const auto& [a, c] : rows)
      if (a == action) return &c;
    return nullptr;
  }
  std::vector<RolltuiChord>& make(std::string_view action) {
    if (std::vector<RolltuiChord>* r = find(action)) return *r;
    rows.emplace_back(std::string(action), std::vector<RolltuiChord>{});
    return rows.back().second;
  }
};

extern "C" {

int rolltui_chord_parse(const char* text, std::size_t len, RolltuiChord* out) {
  RolltuiChord k{};
  const std::string t = lower(std::string_view(text, len));
  // Split on '+', but a trailing "+" alone is the plus character.
  std::vector<std::string> parts;
  std::string cur;
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '+' && !cur.empty() && i + 1 < t.size()) { parts.push_back(cur); cur.clear(); }
    else cur.push_back(t[i]);
  }
  if (cur.empty()) return 0;
  parts.push_back(cur);
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    const std::string& m = parts[i];
    if (m == "ctrl" || m == "control" || m == "c") k.ctrl = 1;
    else if (m == "alt" || m == "meta" || m == "option" || m == "m") k.alt = 1;
    else if (m == "shift" || m == "s") k.shift = 1;
    else return 0;
  }
  const std::string& last = parts.back();
  for (const KeyName& n : kKeyNames)
    if (last == n.name) {
      k.key = n.key;
      if (last == "space") { k.key = ROLLTUI_KEY_CHAR; k.ch = U' '; }
      *out = k;
      return 1;
    }
  const std::vector<rolltui::unicode::DecodedChar> d = rolltui::unicode::decode_utf8(last);
  if (d.size() != 1 || d[0].cp < 0x20) return 0;
  k.key = ROLLTUI_KEY_CHAR;
  k.ch = d[0].cp;
  *out = k;
  return 1;
}

std::size_t rolltui_chord_to_string(const RolltuiChord* k, char* out, std::size_t cap) {
  if (cap < ROLLTUI_CHORD_STRING_MAX) return 0;
  const std::string s = chord_string(*k);
  std::memcpy(out, s.data(), s.size());
  return s.size();
}

std::size_t rolltui_chord_display(const RolltuiChord* k, char* out, std::size_t cap) {
  if (cap < ROLLTUI_CHORD_STRING_MAX) return 0;
  const std::string s = chord_string(*k);
  // Capitalise each part, join with '-': "Ctrl-Shift-Left", "Alt-Enter", "F1", "?".
  std::string res;
  std::string part;
  auto flush = [&]() {
    if (part.empty()) return;
    if (!res.empty()) res += "-";
    if (part.size() > 1 || std::isalpha(static_cast<unsigned char>(part[0])))
      part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
    if (part == "Pageup") part = "PgUp";
    if (part == "Pagedown") part = "PgDn";
    res += part;
    part.clear();
  };
  for (char c : s) {
    if (c == '+' && !part.empty()) flush();
    else part.push_back(c);
  }
  flush();
  std::memcpy(out, res.data(), res.size());
  return res.size();
}

const char* rolltui_bindings_scope_of(const char* action, std::size_t len, std::size_t* out_len) {
  const std::string_view s = scope_view(std::string_view(action, len));
  *out_len = s.size();
  return action;
}

RolltuiBindings* rolltui_bindings_new(void) { return std::make_unique<RolltuiBindings>().release(); }

void rolltui_bindings_free(RolltuiBindings* b) {
  const std::unique_ptr<RolltuiBindings> owned(b);  // takes it back, and frees it on the way out
}

void rolltui_bindings_set_enter_rule(RolltuiBindings* b, const char* action, std::size_t len) {
  b->enter_action.assign(action, len);
}

int rolltui_bindings_has(const RolltuiBindings* b, const char* action, std::size_t len) {
  const std::string_view a(action, len);
  return std::find(b->actions.begin(), b->actions.end(), a) != b->actions.end() ? 1 : 0;
}

void rolltui_bindings_add_action(RolltuiBindings* b, const char* action, std::size_t alen, const char* desc,
                                 std::size_t dlen) {
  if (rolltui_bindings_has(b, action, alen)) return;
  b->actions.emplace_back(action, alen);
  b->descriptions.emplace_back(desc, dlen);
  // A row may already exist with chords in it — declaring is what makes them live and must
  // never throw them away (Bindings.hpp).
  b->make(std::string_view(action, alen));
}

std::size_t rolltui_bindings_action_count(const RolltuiBindings* b) { return b->actions.size(); }

const char* rolltui_bindings_action_at(const RolltuiBindings* b, std::size_t i, std::size_t* len) {
  if (i >= b->actions.size()) { *len = 0; return nullptr; }
  *len = b->actions[i].size();
  return b->actions[i].data();
}

const char* rolltui_bindings_description(const RolltuiBindings* b, const char* action, std::size_t len,
                                         std::size_t* out_len) {
  const std::string_view a(action, len);
  for (std::size_t i = 0; i < b->actions.size(); ++i)
    if (b->actions[i] == a) {
      *out_len = b->descriptions[i].size();
      return b->descriptions[i].data();
    }
  *out_len = 0;
  return nullptr;
}

void rolltui_bindings_undeclare_others(RolltuiBindings* b, RolltuiScopeFn is_library, void* ctx) {
  for (std::size_t i = b->actions.size(); i-- > 0;) {
    const std::string_view scope = scope_view(b->actions[i]);
    if (is_library(ctx, scope.data(), scope.size())) continue;
    b->actions.erase(b->actions.begin() + static_cast<std::ptrdiff_t>(i));
    b->descriptions.erase(b->descriptions.begin() + static_cast<std::ptrdiff_t>(i));
  }
}

std::size_t rolltui_bindings_row_count(const RolltuiBindings* b) { return b->rows.size(); }

const char* rolltui_bindings_row_at(const RolltuiBindings* b, std::size_t i, std::size_t* len) {
  if (i >= b->rows.size()) { *len = 0; return nullptr; }
  *len = b->rows[i].first.size();
  return b->rows[i].first.data();
}

int rolltui_bindings_has_row(const RolltuiBindings* b, const char* action, std::size_t len) {
  return b->find(std::string_view(action, len)) != nullptr ? 1 : 0;
}

void rolltui_bindings_add_row(RolltuiBindings* b, const char* action, std::size_t len) {
  b->make(std::string_view(action, len));
}

std::size_t rolltui_bindings_chord_count(const RolltuiBindings* b, const char* action, std::size_t len) {
  const std::vector<RolltuiChord>* r = b->find(std::string_view(action, len));
  return r ? r->size() : 0;
}

int rolltui_bindings_chord_at(const RolltuiBindings* b, const char* action, std::size_t len, std::size_t i,
                              RolltuiChord* out) {
  const std::vector<RolltuiChord>* r = b->find(std::string_view(action, len));
  if (!r || i >= r->size()) return 0;
  *out = (*r)[i];
  return 1;
}

const char* rolltui_bindings_action_for(const RolltuiBindings* b, const RolltuiChord* k, const char* scope,
                                        std::size_t scope_len, std::size_t* out_len) {
  const unsigned char p = rolltui_key_active_protocol();
  const std::string_view want(scope, scope_len);
  for (const auto& [a, chords] : b->rows) {
    if (scope_view(a) != want) continue;
    if (!rolltui_bindings_has(b, a.data(), a.size())) continue;  // kept, never emitted
    for (const RolltuiChord& c : chords)
      // A chord this terminal cannot deliver is kept and inert for the same reason: the
      // row survives save, and nothing can emit it, so it must not claim a key.
      if (chord_eq(c, *k) && rolltui_key_deliverable(&c, p)) {
        *out_len = a.size();
        return a.data();
      }
  }
  *out_len = 0;
  return nullptr;
}

int rolltui_bindings_breaks_enter_rule(const RolltuiBindings* b, const char* action, std::size_t len,
                                       const RolltuiChord* chord) {
  const std::string_view a(action, len);
  if (b->enter_action.empty() || !is_bare_enter(*chord)) return 0;
  if (a == b->enter_action) return 0;
  return scope_view(a) == scope_view(b->enter_action) ? 1 : 0;
}

int rolltui_bindings_bind(RolltuiBindings* b, const char* action, std::size_t len, const RolltuiChord* chord,
                          const char** moved_from, std::size_t* moved_len) {
  const std::string_view a(action, len);
  if (!rolltui_bindings_has(b, action, len)) return 0;
  if (rolltui_bindings_breaks_enter_rule(b, action, len, chord)) return 0;
  for (auto& [other, chords] : b->rows) {
    if (other == a || scope_view(other) != scope_view(a)) continue;
    const auto it = std::find_if(chords.begin(), chords.end(), [&](const RolltuiChord& c) { return chord_eq(c, *chord); });
    if (it == chords.end()) continue;
    if (is_bare_enter(*chord) && other == b->enter_action) return 0;  // never away from submit
    chords.erase(it);
    if (moved_from) { *moved_from = other.data(); *moved_len = other.size(); }
  }
  std::vector<RolltuiChord>& mine = b->make(a);
  if (std::none_of(mine.begin(), mine.end(), [&](const RolltuiChord& c) { return chord_eq(c, *chord); }))
    mine.push_back(*chord);
  return 1;
}

void rolltui_bindings_add_chord(RolltuiBindings* b, const char* action, std::size_t len, const RolltuiChord* chord) {
  std::vector<RolltuiChord>& mine = b->make(std::string_view(action, len));
  if (std::none_of(mine.begin(), mine.end(), [&](const RolltuiChord& c) { return chord_eq(c, *chord); }))
    mine.push_back(*chord);
}

int rolltui_bindings_unbind(RolltuiBindings* b, const char* action, std::size_t len, const RolltuiChord* chord) {
  const std::string_view a(action, len);
  if (is_bare_enter(*chord) && a == b->enter_action) return 0;
  std::vector<RolltuiChord>* r = b->find(a);
  if (!r) return 0;
  const auto it = std::find_if(r->begin(), r->end(), [&](const RolltuiChord& c) { return chord_eq(c, *chord); });
  if (it == r->end()) return 0;
  r->erase(it);
  return 1;
}

void rolltui_bindings_clear(RolltuiBindings* b, const char* action, std::size_t len) {
  const std::string_view a(action, len);
  if (a == b->enter_action) return;
  if (std::vector<RolltuiChord>* r = b->find(a)) r->clear();
}

RolltuiBindings* rolltui_bindings_clone(const RolltuiBindings* b) {
  std::unique_ptr<RolltuiBindings> out = std::make_unique<RolltuiBindings>(*b);
  return out.release();
}

int rolltui_bindings_equal(const RolltuiBindings* a, const RolltuiBindings* b) {
  if (a == b) return 1;
  if (!a || !b || a->rows.size() != b->rows.size()) return 0;
  for (std::size_t i = 0; i < a->rows.size(); ++i) {
    if (a->rows[i].first != b->rows[i].first) return 0;
    if (a->rows[i].second.size() != b->rows[i].second.size()) return 0;
    for (std::size_t j = 0; j < a->rows[i].second.size(); ++j)
      if (!chord_eq(a->rows[i].second[j], b->rows[i].second[j])) return 0;
  }
  return 1;
}

}  // extern "C"
