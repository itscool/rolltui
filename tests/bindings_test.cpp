//
// bindings_test.cpp — key bindings as data (milestone 17): chords parse and print in
// every modifier order and round-trip; the shipped default embeds its file verbatim,
// loads clean, and binds every library action; lookup is by scope so one chord serves
// several widgets; a conflict inside a scope is reported and the first binding wins;
// the Enter rule refuses a file that moves Enter and restores it; bind() moves a chord
// off a conflicting action and says so; to_json round-trips; help_lines renders the
// live table. Phase 10 m4: the app scope leaves the library's table — a layout
// declares it, a file's chords for an undeclared action are kept and inert, and
// declaring makes them live. Phase 11 m1: the library's own TOOLS' scopes leave it
// too — one declare() takes the layout's actions and the mounted tool's, a tool's
// suggested chord fills a gap and never overrides, and a file naming an
// unmounted tool's action still loads clean and keeps its row.
// Phase 12 m3: a chord this terminal cannot deliver is refused BY NAME and its row kept
// anyway — the loader's half of it; the deliverability model itself is measured against
// the encodings in deliverability_test.
//
// PHASE 17 m2: converted off the C++ shim (`rolltui/Bindings.hpp`/`Bindings.cpp`), which
// is being deleted — this file now calls `rolltui/c/rolltui_bindings.h` (and friends)
// directly, reached only through the umbrella `rolltui/rolltui.h`. The shim's own
// composition logic (the action vocabulary, `declare()`/`suggest()`, the shipped-default
// assembly) has no home yet on the C side — the header comment at `rolltui_bindings.h`
// says it stays test-adjacent vocabulary — so it is reproduced here as local fixture code,
// mirroring `Bindings.cpp` line for line, per the instruction to treat the shim as the
// mapping rather than guess at one.
//
#include <algorithm>
#include "rolltui/c/rolltui_embedded.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <type_traits>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_BINDINGS_DIR
#error "ROLLTUI_BINDINGS_DIR must point at rolltui/presets/bindings"
#endif

// The embedded preset tables themselves: permanent generated C++ data
// (rolltui/cmake/embed_presets.cmake), redeclared here exactly as Bindings.cpp,
// Layout.cpp and Presets.cpp each already do independently. Declared before every use
// below — an `extern` forward declaration has to precede the code that reads it.

// `RolltuiChord` (rolltui/c/rolltui_keys.h) deliberately has no `operator==` of its own —
// unlike `RolltuiMouseEvent`/`RolltuiRect`/`RolltuiCell`, nothing in the C API compares two
// chords directly (the loader/lookup functions take one chord at a time). This test wants
// `std::optional<RolltuiChord>` equality throughout, exactly as the shim's `KeyEvent` gave
// it, so it is added here — at TRUE FILE SCOPE and not inside an unnamed namespace: an
// unnamed-namespace operator== is invisible to the ADL that `std::optional`'s own
// comparison operators perform from within `namespace std`, which is a real, checked
// finding (confirmed with a throwaway compile) and not a style choice.
bool operator==(const RolltuiChord& a, const RolltuiChord& b) {
  return a.key == b.key && a.ch == b.ch && a.ctrl == b.ctrl && a.alt == b.alt && a.shift == b.shift;
}

namespace {

RolltuiChord key(unsigned char k, bool ctrl = false, bool alt = false, bool shift = false) {
  RolltuiChord e{};
  e.key = k;
  e.ctrl = ctrl;
  e.alt = alt;
  e.shift = shift;
  return e;
}
RolltuiChord ch(char32_t c, bool ctrl = false, bool alt = false) {
  RolltuiChord e{};
  e.key = ROLLTUI_KEY_CHAR;
  e.ch = c;
  e.ctrl = ctrl;
  e.alt = alt;
  return e;
}
std::string read_file(const std::string& p) { std::ifstream in(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }

// ---- chord helpers over rolltui_bindings.h / rolltui_keys.h, CALLER-FILLED as the header
// states (three modifiers plus the longest key name is a stated constant, so there is no
// measure-then-fill) — mirrors Bindings.cpp's own three one-liners. ----
std::optional<RolltuiChord> parse_chord(std::string_view text) {
  RolltuiChord c{};
  if (!rolltui_chord_parse(text.data(), text.size(), &c)) return std::nullopt;
  return c;
}
std::string chord_to_string(const RolltuiChord& k) {
  char buf[ROLLTUI_CHORD_STRING_MAX];
  return std::string(buf, rolltui_chord_to_string(&k, buf, sizeof buf));
}
std::string chord_display(const RolltuiChord& k) {
  char buf[ROLLTUI_CHORD_STRING_MAX];
  return std::string(buf, rolltui_chord_display(&k, buf, sizeof buf));
}

// ---- the library's 59 actions, READ FROM THE C rather than copied ----------------------
// This file used to carry a verbatim duplicate of the whole table. It moved into
// `c/rolltui_library_actions.c` on 2026-09-04 precisely because four consumers had each
// grown one — the vocabulary a "written down twice drifts" rule was protecting had become
// quintuple. Now there is one table and this reads it.
struct ActionInfo {
  std::string_view name, description;  // BORROWS into the C's static literals
};
const std::vector<ActionInfo>& library_actions() {
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
  for (const ActionInfo& a : library_actions())
    if (scope_of(a.name) == scope) return true;
  return false;
}

// ---- the two vocabulary callbacks rolltui_bindings_load_json asks through ----
int is_library_scope_cb(void*, const char* scope, std::size_t len) {
  return library_scope(std::string_view(scope, len)) ? 1 : 0;
}
std::size_t reason_cb(void*, const RolltuiChord* k, unsigned char protocol, char* out, std::size_t cap) {
  // THE LIBRARY'S SENTENCE (Phase 17 m2a). This was a verbatim copy of the six, made because
  // they lived in `Keys.cpp` and a C consumer could not reach them; there were three.
  const int code = rolltui_key_undeliverable_reason(k, protocol);
  std::size_t rlen = 0;
  const char* rp = rolltui_key_undeliverable_text(code, &rlen);
  const std::string_view r(rp, rlen);
  const std::size_t n = std::min(r.size(), cap);
  std::memcpy(out, r.data(), n);
  return n;
}

// ---- mirrors rolltui::BindingsLoadReport (Bindings.hpp) and copy_report (Bindings.cpp). ----
struct BindingsLoadReport {
  std::string error;
  std::vector<std::string> unknown_actions;
  std::vector<std::string> bad_chords;
  std::vector<std::string> undeliverable;
  std::vector<std::string> conflicts;
  std::vector<std::string> bad_values;
  std::vector<std::string> unknown_keys;
  bool clean() const {
    return error.empty() && unknown_actions.empty() && bad_chords.empty() && conflicts.empty() && bad_values.empty() &&
           unknown_keys.empty() && undeliverable.empty();
  }
  std::string summary() const {
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
};
void copy_report(BindingsLoadReport& out, const RolltuiBindingsReport& in) {
  auto copy = [](const RolltuiStr* v, std::size_t n) {
    std::vector<std::string> r;
    r.reserve(n);
    for (std::size_t i = 0; i < n; ++i) r.emplace_back(view_of(v[i]));
    return r;
  };
  out.error = str_of(in.error);
  out.unknown_actions = copy(in.unknown_actions, in.unknown_actions_n);
  out.bad_chords = copy(in.bad_chords, in.bad_chords_n);
  out.undeliverable = copy(in.undeliverable, in.undeliverable_n);
  out.conflicts = copy(in.conflicts, in.conflicts_n);
  out.bad_values = copy(in.bad_values, in.bad_values_n);
  out.unknown_keys = copy(in.unknown_keys, in.unknown_keys_n);
}

constexpr std::string_view kEnterAction = "input.submit";

// ---- the "Bindings" composition, as free functions over an owned RolltuiBindings* — the
// shim's methods, mirrored one for one. Every RolltuiBindings* returned here is OWNED by
// the caller (rolltui_bindings_new/_clone underneath); every use below frees what it made. ----
RolltuiBindings* bindings_new() {  // mirrors Bindings::Bindings()
  RolltuiBindings* b = rolltui_bindings_new();
  rolltui_bindings_set_enter_rule(b, kEnterAction.data(), kEnterAction.size());
  for (const ActionInfo& a : library_actions())
    rolltui_bindings_add_action(b, a.name.data(), a.name.size(), a.description.data(), a.description.size());
  return b;
}

std::string_view bindings_holder(const RolltuiBindings* b, const RolltuiChord& chord, std::string_view scope) {
  const std::size_t rows = rolltui_bindings_row_count(b);
  for (std::size_t i = 0; i < rows; ++i) {
    std::size_t alen = 0;
    const char* a = rolltui_bindings_row_at(b, i, &alen);
    if (scope_of(std::string_view(a, alen)) != scope) continue;
    const std::size_t n = rolltui_bindings_chord_count(b, a, alen);
    for (std::size_t j = 0; j < n; ++j) {
      RolltuiChord c{};
      if (rolltui_bindings_chord_at(b, a, alen, j, &c) && c == chord) return std::string_view(a, alen);
    }
  }
  return {};
}

struct ToolAction {
  std::string_view name, description, chord;
};
void bindings_suggest(RolltuiBindings* b, const std::vector<ToolAction>& tools) {
  for (const ToolAction& t : tools) {
    if (rolltui_bindings_has_row(b, t.name.data(), t.name.size())) continue;
    const std::optional<RolltuiChord> k = parse_chord(t.chord);
    const bool taken = k && !bindings_holder(b, *k, scope_of(t.name)).empty();
    rolltui_bindings_add_row(b, t.name.data(), t.name.size());
    if (k && !taken) {
      const RolltuiChord c = *k;
      rolltui_bindings_add_chord(b, t.name.data(), t.name.size(), &c);
    }
  }
}

struct ActionDecl {
  std::string name, description;
};
void bindings_declare(RolltuiBindings* b, const std::vector<ActionDecl>& declared, const std::vector<ToolAction>& tools = {}) {
  bindings_suggest(b, tools);
  rolltui_bindings_undeclare_others(b, is_library_scope_cb, nullptr);
  for (const ActionDecl& d : declared) rolltui_bindings_add_action(b, d.name.data(), d.name.size(), d.description.data(), d.description.size());
  for (const ToolAction& t : tools) rolltui_bindings_add_action(b, t.name.data(), t.name.size(), t.description.data(), t.description.size());
}

std::vector<std::string> bindings_undeclared(const RolltuiBindings* b) {
  std::vector<std::string> out;
  const std::size_t rows = rolltui_bindings_row_count(b);
  for (std::size_t i = 0; i < rows; ++i) {
    std::size_t alen = 0;
    const char* a = rolltui_bindings_row_at(b, i, &alen);
    if (rolltui_bindings_chord_count(b, a, alen) == 0) continue;
    if (rolltui_bindings_has(b, a, alen)) continue;
    out.emplace_back(a, alen);
  }
  return out;
}

std::vector<std::string> bindings_actions(const RolltuiBindings* b) {
  std::vector<std::string> out;
  const std::size_t n = rolltui_bindings_action_count(b);
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t len = 0;
    const char* p = rolltui_bindings_action_at(b, i, &len);
    out.emplace_back(p, len);
  }
  return out;
}

std::vector<RolltuiChord> bindings_chords_for(const RolltuiBindings* b, std::string_view action) {
  const std::size_t n = rolltui_bindings_chord_count(b, action.data(), action.size());
  std::vector<RolltuiChord> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord c{};
    if (rolltui_bindings_chord_at(b, action.data(), action.size(), i, &c)) out.push_back(c);
  }
  return out;
}

std::string bindings_chords_text(const RolltuiBindings* b, std::string_view action) {
  const unsigned char p = rolltui_key_active_protocol();
  const std::size_t n = rolltui_bindings_chord_count(b, action.data(), action.size());
  std::string s;
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord c{};
    if (!rolltui_bindings_chord_at(b, action.data(), action.size(), i, &c)) continue;
    if (!rolltui_key_deliverable(&c, p)) continue;
    char buf[ROLLTUI_CHORD_STRING_MAX];
    const std::size_t len = rolltui_chord_display(&c, buf, sizeof buf);
    if (!s.empty()) s += ", ";
    s.append(buf, len);
  }
  return s;
}

std::string_view bindings_action_for(const RolltuiBindings* b, const RolltuiChord& k, std::string_view scope) {
  std::size_t len = 0;
  const char* a = rolltui_bindings_action_for(b, &k, scope.data(), scope.size(), &len);
  return a ? std::string_view(a, len) : std::string_view();
}

bool bindings_bind(RolltuiBindings* b, std::string_view action, const RolltuiChord& chord, std::string* moved_from = nullptr) {
  const char* moved = nullptr;
  std::size_t moved_len = 0;
  if (!rolltui_bindings_bind(b, action.data(), action.size(), &chord, &moved, &moved_len)) return false;
  if (moved && moved_from) moved_from->assign(moved, moved_len);
  return true;
}

bool bindings_unbind(RolltuiBindings* b, std::string_view action, const RolltuiChord& chord) {
  return rolltui_bindings_unbind(b, action.data(), action.size(), &chord) != 0;
}

void bindings_clear(RolltuiBindings* b, std::string_view action) { rolltui_bindings_clear(b, action.data(), action.size()); }
void bindings_add_action(RolltuiBindings* b, std::string_view action, std::string_view desc) {
  rolltui_bindings_add_action(b, action.data(), action.size(), desc.data(), desc.size());
}
std::string_view bindings_description(const RolltuiBindings* b, std::string_view action) {
  std::size_t len = 0;
  const char* p = rolltui_bindings_description(b, action.data(), action.size(), &len);
  return p ? std::string_view(p, len) : std::string_view();
}
bool bindings_has(const RolltuiBindings* b, std::string_view action) {
  return rolltui_bindings_has(b, action.data(), action.size()) != 0;
}
bool bindings_equal(const RolltuiBindings* a, const RolltuiBindings* b) { return rolltui_bindings_equal(a, b) != 0; }

std::string bindings_to_json(const RolltuiBindings* b, std::string_view name) {
  RolltuiStr out;
  rolltui_bindings_dump_json(b, name.data(), name.size(), &out);
  return str_of(out);
}

// Mirrors Bindings::from_json, minus the json::Value round trip that existed only for a
// DIFFERENT caller (Presets.cpp) already holding a parsed tree — every use in this file
// hands from_json TEXT (either a literal or bindings_to_json's own output), so the
// text-only path is the whole of what this test needs.
RolltuiBindings* bindings_from_json(std::string_view text, BindingsLoadReport& report, unsigned char deliver) {
  RolltuiBindings* b = bindings_new();
  RolltuiBindingsReport rep{};
  const int ok = rolltui_bindings_load_json(b, text.data(), text.size(), deliver, is_library_scope_cb, nullptr,
                                            reason_cb, nullptr, &rep);
  copy_report(report, rep);
  rolltui_bindings_report_release(&rep);
  if (!ok) {
    rolltui_bindings_free(b);
    return nullptr;
  }
  return b;
}
RolltuiBindings* bindings_from_json(std::string_view text, BindingsLoadReport& report) {
  return bindings_from_json(text, report, rolltui_key_active_protocol());
}

// ---- the shipped default: mirrors Bindings.cpp's default_bindings_json/
// shipped_default_actions/default_bindings, reaching the same embedded C++ data tables
// those functions did (declared here exactly as Bindings.cpp/Layout.cpp/Presets.cpp each
// independently redeclare them — the tables are permanent generated data, not shim logic).
// No cache: unlike the shim's `const Bindings&` singleton, this returns a fresh OWNED
// table each call, which every call site below frees — building it a dozen times in a
// test is free next to what deliverability_test already spends. ----

std::string_view default_bindings_json() {
  for (std::size_t i = 0; i < rolltui_kBindingsPresetCount; ++i)
    if (std::string_view(rolltui_kBindingsPresets[i].name) == "default") return rolltui_kBindingsPresets[i].text;
  return "";
}

std::string_view builtin_layout_json(std::string_view name) {
  for (std::size_t i = 0; i < rolltui_kLayoutPresetCount; ++i)
    if (std::string_view(rolltui_kLayoutPresets[i].name) == name) return rolltui_kLayoutPresets[i].text;
  return "";
}

const std::vector<ActionDecl>& shipped_default_actions() {
  static const std::vector<ActionDecl> decls = [] {
    std::vector<ActionDecl> out;
    const std::string_view text = builtin_layout_json("default");
    RolltuiJsonValue* v = rolltui_json_parse(text.data(), text.size(), nullptr);
    if (v) {
      RolltuiLayoutAction* actions = nullptr;
      std::size_t n = 0, cap = 0;
      rolltui_layout_read_actions_key(v, &actions, &n, &cap);
      out.reserve(n);
      for (std::size_t i = 0; i < n; ++i) out.push_back({str_of(actions[i].name), str_of(actions[i].description)});
      rolltui_layout_actions_free(actions, n);
      rolltui_json_free(v);
    }
    return out;
  }();
  return decls;
}

RolltuiBindings* default_bindings() {
  BindingsLoadReport rep;
  RolltuiBindings* d = bindings_from_json(default_bindings_json(), rep, ROLLTUI_PROTOCOL_LEGACY);
  if (!d || !rep.clean()) {
    std::fprintf(stderr, "rolltui: the shipped default bindings are broken: %s\n", rep.summary().c_str());
    std::abort();
  }
  bindings_declare(d, shipped_default_actions());
  if (const std::vector<std::string> dead = bindings_undeclared(d); !dead.empty()) {
    std::fprintf(stderr,
                 "rolltui: the shipped default bindings bind '%s', which no shipped layout declares (a mounted "
                 "tool's chords belong to the tool)\n",
                 dead.front().c_str());
    std::abort();
  }
  return d;
}

// THE LIBRARY'S RULE, not a copy (Phase 17 m2a). This was a verbatim reimplementation of the
// chord column's width, its 22/12 caps and "(unbound)" — so these checks asserted against the
// test's own arithmetic, not against the one the help window draws.
std::vector<std::string> help_lines(const RolltuiBindings* b, std::string_view scope, const std::vector<std::string>& actions = {}) {
  std::vector<const char*> ptrs;
  std::vector<std::size_t> lens;
  for (const std::string& a : actions) { ptrs.push_back(a.data()); lens.push_back(a.size()); }
  RolltuiStr buf{};
  rolltui_help_scope_lines(b, scope.data(), scope.size(), ptrs.data(), lens.data(), ptrs.size(), nullptr, 0, &buf);
  std::vector<std::string> out;
  const std::string_view all(buf.p ? buf.p : "", buf.n);
  for (std::size_t i = 0; i < all.size();) {
    const std::size_t nl = all.find('\n', i);
    out.emplace_back(all.substr(i, nl - i));
    i = nl + 1;
  }
  rolltui_str_free(&buf);
  return out;
}

}  // namespace

int main() {
  // ---- chords ----
  check(parse_chord("ctrl+w") == ch('w', true) && parse_chord("Ctrl+W") == ch('w', true) && parse_chord("C+w") == ch('w', true), "ctrl+w in any case or abbreviation");
  check(parse_chord("shift+ctrl+left") == key(ROLLTUI_KEY_LEFT, true, false, true) && parse_chord("ctrl+shift+left") == key(ROLLTUI_KEY_LEFT, true, false, true), "modifiers in any order");
  check(parse_chord("alt+enter") == key(ROLLTUI_KEY_ENTER, false, true) && parse_chord("meta+enter") == key(ROLLTUI_KEY_ENTER, false, true) && parse_chord("option+enter") == key(ROLLTUI_KEY_ENTER, false, true), "alt, meta and option are one modifier");
  check(parse_chord("f1") == key(ROLLTUI_KEY_F1) && parse_chord("F12") == key(ROLLTUI_KEY_F12) && parse_chord("escape") == key(ROLLTUI_KEY_ESCAPE) && parse_chord("esc") == key(ROLLTUI_KEY_ESCAPE) && parse_chord("pgdn") == key(ROLLTUI_KEY_PAGEDOWN), "named keys and their short forms");
  check(parse_chord("?") == ch(U'?') && parse_chord("space") == ch(U' ') && parse_chord("+") == ch(U'+') && parse_chord("ctrl++") == ch(U'+', true), "a printable character is itself; '+' and 'ctrl++' parse");
  check(!parse_chord("") && !parse_chord("ctrl+") && !parse_chord("hyper+x") && !parse_chord("ctrl+meta+x+y") && !parse_chord("f13"), "empty, dangling, unknown modifier, two keys, f13: not chords");
  check(chord_to_string(key(ROLLTUI_KEY_LEFT, true, false, true)) == "ctrl+shift+left" && chord_to_string(ch(U'?')) == "?" && chord_to_string(ch(U' ')) == "space" && chord_to_string(key(ROLLTUI_KEY_PAGEUP)) == "pageup",
        "chord_to_string is canonical (ctrl, alt, shift; key names)");
  {
    bool all = true;
    for (const char* c : {"ctrl+shift+left", "alt+enter", "f1", "escape", "?", "space", "shift+tab", "ctrl+home", "alt+backspace", "pagedown"})
      all &= chord_to_string(*parse_chord(c)) == c;
    check(all, "canonical chords round-trip through parse and print");
    // THE GUARANTEE IS STILL RUNTIME-TESTABLE, and the first conversion of this file made it
    // vacuous by accident — worth the ten lines to say why. `RolltuiChord` has no `raw` field,
    // so there is nothing to attach bytes TO, and the converted assertion had degenerated into
    // a second `chord_to_string` check wearing this message. But the property was never about
    // the chord's fields: it is that a decoded event carries its payload in the ENVELOPE
    // (`RolltuiEvent::text`) and its identity in the chord, and the two do not mix. That is
    // exactly what an Unknown key produces, so drive the real decoder and look.
    {
      struct Seen {
        RolltuiChord chord{};
        std::string text;
        bool got = false;
      } seen;
      RolltuiKeyDecoder* dec = rolltui_key_decoder_new();
      // An unrecognised CSI: the decoder emits it as Unknown, with the bytes as `text`.
      const std::string bytes = "\x1b[9999~";
      rolltui_key_decoder_feed(
          dec, bytes.data(), bytes.size(),
          [](void* ctx, const RolltuiEvent* e) {
            Seen& s = *static_cast<Seen*>(ctx);
            if (s.got || e->kind != ROLLTUI_EVENT_KEY) return;
            s.chord = e->key;
            s.text.assign(e->text ? e->text : "", e->text_len);
            s.got = true;
          },
          &seen);
      rolltui_key_decoder_flush(dec, [](void*, const RolltuiEvent*) {}, nullptr);
      rolltui_key_decoder_free(dec);
      // THE ARMING HALF: the payload must actually BE there, or the assertion below is the
      // vacuous one again in a new costume.
      check(seen.got && !seen.text.empty(),
            "an Unknown key really does carry its raw bytes in the event [" + seen.text.substr(1) + "]");
      // …and the chord it came with carries none of them: same key, no ctrl/alt/shift, and a
      // zero codepoint. The bytes are in the envelope and nowhere else.
      const RolltuiChord bare = key(seen.chord.key, false);
      check(seen.chord.key == bare.key && seen.chord.ch == 0 && !seen.chord.ctrl && !seen.chord.alt &&
                !seen.chord.shift,
            "a KeyEvent's raw bytes are never part of a chord");
    }
    check(chord_display(*parse_chord("ctrl+shift+left")) == "Ctrl-Shift-Left" && chord_display(*parse_chord("alt+enter")) == "Alt-Enter" && chord_display(*parse_chord("?")) == "?" &&
              chord_display(*parse_chord("pageup")) == "PgUp" && chord_display(*parse_chord("f1")) == "F1",
          "chord_display is the help form (Ctrl-Shift-Left, Alt-Enter, PgUp)");
    check(chord_to_string(key(ROLLTUI_KEY_UNKNOWN)).empty(), "an Unknown key has no chord");
  }
  // ---- the shipped default ----
  {
    check(default_bindings_json() == read_file(std::string(ROLLTUI_BINDINGS_DIR) + "/default.json") && !default_bindings_json().empty(), "the default embeds presets/bindings/default.json verbatim");
    BindingsLoadReport rep;
    RolltuiBindings* d = bindings_from_json(default_bindings_json(), rep);
    check(d && rep.clean(), "the shipped default loads clean [" + rep.summary() + "]");
    bool every_bound = true;
    std::string unbound;
    for (const std::string& a : bindings_actions(d))
      if (bindings_chords_for(d, a).empty()) { every_bound = false; unbound += " " + a; }
    check(every_bound, "every library action has at least one chord in the default" + unbound);
    check(bindings_actions(d).size() == library_actions().size(), "the table lists exactly the library's actions (" + std::to_string(bindings_actions(d).size()) + ")");
    rolltui_bindings_free(d);
    RolltuiBindings* b = default_bindings();
    check(bindings_action_for(b, key(ROLLTUI_KEY_ENTER), "input") == "input.submit" && bindings_action_for(b, key(ROLLTUI_KEY_ENTER), "menu") == "menu.activate" && bindings_action_for(b, key(ROLLTUI_KEY_ENTER), "transcript").empty(),
          "Enter is input.submit in the input scope, menu.activate in the menu scope, nothing in the transcript");
    check(bindings_action_for(b, key(ROLLTUI_KEY_UP), "input") == "input.up" && bindings_action_for(b, key(ROLLTUI_KEY_UP), "transcript") == "transcript.line_up" && bindings_action_for(b, key(ROLLTUI_KEY_UP), "menu") == "menu.up",
          "Up serves three scopes");
    check(bindings_action_for(b, ch('w', true), "input") == "input.kill_word_backward" && bindings_action_for(b, key(ROLLTUI_KEY_BACKSPACE, false, true), "input") == "input.kill_word_backward", "two chords, one action");
    check(bindings_action_for(b, key(ROLLTUI_KEY_LEFT, true, false, true), "input") == "input.select_word_left" && bindings_action_for(b, key(ROLLTUI_KEY_LEFT, false, true, true), "input") == "input.select_word_left", "ctrl+shift+left and alt+shift+left both extend by a word");
    check(bindings_action_for(b, ch('z', true), "input") == "input.undo" && bindings_action_for(b, ch('y', true), "input") == "input.redo", "Ctrl+Z undoes, Ctrl+Y redoes (Phase 12 m1)");
    check(bindings_action_for(b, ch(U'?'), "app") == "app.help" && bindings_action_for(b, key(ROLLTUI_KEY_F1), "app") == "app.help" && bindings_action_for(b, ch(U'?'), "input").empty(), "'?' is app.help and is not an input action (typing it inserts)");
    // The lookup half of the same property. A chord cannot carry bytes, so what is checked is
    // that the ENVELOPE's payload never reaches the table: an event decoded WITH text resolves
    // by its chord alone. Ctrl+W is typed here as a plain chord because that is precisely what
    // `action_for` receives — the decoder having already put any payload elsewhere.
    RolltuiChord with_raw = ch('w', true);
    // The structural half is TRIVIAL COPYABILITY, not a size: a type that is trivially
    // copyable cannot own a heap buffer, so it cannot carry bytes however hard a caller
    // tries. `std::string raw` — what this chord used to hold — would make it false. A size
    // assertion would only have checked padding.
    static_assert(std::is_trivially_copyable<RolltuiChord>::value,
                  "a chord that could own bytes is a chord that could carry them into a lookup");
    check(bindings_action_for(b, with_raw, "input") == "input.kill_word_backward",
          "lookup ignores raw bytes");
    check(bindings_chords_text(b, "input.kill_word_backward") == "Ctrl-W, Alt-Backspace", "chords_text joins the display forms [" + bindings_chords_text(b, "input.kill_word_backward") + "]");
    check(scope_of("input.submit") == "input" && scope_of("app.help") == "app", "scope_of");
    rolltui_bindings_free(b);
  }
  // ---- Phase 10 m4: the app scope is a LAYOUT's, not the library's ----
  {
    bool any_app = false;
    for (const ActionInfo& a : library_actions()) any_app |= scope_of(a.name) == "app";
    check(!any_app, "library_actions() declares no app.* action — the layout does (m4)");
    check(library_scope("input") && library_scope("transcript") && library_scope("menu") && library_scope("edit") && library_scope("stack") &&
              !library_scope("app") && !library_scope("editor") && !library_scope("studio") && !library_scope("mine"),
          "library_scope: the WIDGET scopes are the library's; app, the tools' and a host's own are not (Phase 11 m1)");

    // A file's chords for an undeclared action are KEPT and inert, never dropped: a
    // bindings file is global and a user's, while the actions are the screen's.
    BindingsLoadReport rep;
    RolltuiBindings* b = bindings_from_json(R"({"name":"x","bindings":{"app.help":["f1","?"],"other.thing":["f9"],"input.sumbit":["f8"]}})", rep);
    check(b && rep.unknown_actions == std::vector<std::string>{"input.sumbit"},
          "a typo in a LIBRARY scope is an unknown action; a name in any other scope is not");
    check(b && !bindings_has(b, "app.help") && bindings_action_for(b, key(ROLLTUI_KEY_F1), "app").empty() && bindings_chords_for(b, "app.help").size() == 2,
          "an undeclared action keeps its chords and never answers a key");
    check(b && bindings_undeclared(b) == std::vector<std::string>{"app.help", "other.thing"}, "undeclared() names them, in table order");
    BindingsLoadReport rep2;
    RolltuiBindings* round = bindings_from_json(bindings_to_json(b, "x"), rep2);
    check(round && bindings_chords_for(round, "app.help").size() == 2 && bindings_equal(round, b),
          "…and they survive the round trip, so a file written on one screen keeps its keys on another");
    rolltui_bindings_free(round);

    bindings_declare(b, {{"app.help", "open help"}});
    check(bindings_has(b, "app.help") && bindings_action_for(b, key(ROLLTUI_KEY_F1), "app") == "app.help" && bindings_chords_for(b, "app.help").size() == 2 &&
              bindings_description(b, "app.help") == "open help",
          "declaring the action makes the kept chords live, with its description");
    check(bindings_undeclared(b) == std::vector<std::string>{"other.thing"}, "…and only the still-undeclared ones remain");
    bindings_declare(b, {{"app.help", "SOMETHING ELSE"}});
    check(bindings_description(b, "app.help") == "SOMETHING ELSE",
          "re-declaring updates the description — a hot-reloaded layout file may change what an action does");
    bindings_declare(b, {{"app.help", "open help"}});
    check(help_lines(b, "app").size() == 1 && help_lines(b, "app")[0].find("F1, ?") == 0,
          "help renders an action known only because a layout declared it [" + (help_lines(b, "app").empty() ? "" : help_lines(b, "app")[0]) + "]");

    // m6: declare() is AUTHORITATIVE, not additive — the actions of the screen you are on,
    // not of every screen you have been on. Found by the files-only proof: a runtime layout
    // switch left the previous layout's five app actions live under a layout declaring one.
    bindings_declare(b, {{"app.zoom", "zoom in"}});
    check(!bindings_has(b, "app.help") && bindings_action_for(b, key(ROLLTUI_KEY_F1), "app").empty() && bindings_chords_for(b, "app.help").size() == 2 &&
              bindings_has(b, "app.zoom") && help_lines(b, "app").size() == 1,
          "a layout that stops declaring an action makes it inert again — its chords kept, nothing emitting it");
    check(bindings_has(b, "input.submit") && bindings_has(b, "menu.activate") && bindings_has(b, "stack.close_popup"),
          "…and the library's own closed scopes are untouched by any declaration");
    bindings_declare(b, {{"app.help", "open help"}});  // put the block's screen back for what follows

    // Two UNDECLARED actions of one scope still conflict at load — the check runs over
    // the rows, not through action_for, which skips them.
    BindingsLoadReport rep3;
    RolltuiBindings* c2 = bindings_from_json(R"({"name":"c","bindings":{"app.one":["f9"],"app.two":["f9"]}})", rep3);
    check(c2 && rep3.conflicts.size() == 1 && rep3.conflicts[0].find("'f9' bound to both app.one and app.two") == 0,
          "a chord bound twice in one undeclared scope is still a conflict");
    rolltui_bindings_free(c2);

    // default_bindings() is the shipped bindings over the shipped default LAYOUT.
    RolltuiBindings* d2 = default_bindings();
    check(bindings_has(d2, "app.help") && bindings_description(d2, "app.menu") == "open the settings and commands menu",
          "default_bindings() carries the shipped default layout's declared actions");
    check(bindings_actions(d2).size() == library_actions().size() + shipped_default_actions().size(),
          "…exactly those and the library's, nothing else");
    check(bindings_undeclared(d2).empty(), "…and the shipped bindings bind nothing the shipped layouts do not declare");
    rolltui_bindings_free(d2);
    rolltui_bindings_free(b);
  }
  // ---- Phase 11 m1: a TOOL's scope is not the library's ----------------------------
  // library_actions() closes over the WIDGET scopes only. `editor.*` and `studio.*`
  // are the library's own tools' — one application's, not every host's — so whoever
  // MOUNTS a tool declares them, and a host that mounts none advertises none.
  {
    bool tool_scope = false;
    std::string named;
    for (const ActionInfo& a : library_actions())
      if (const std::string_view s = scope_of(a.name); s == "editor" || s == "studio" || s == "app") { tool_scope = true; named = a.name; }
    check(!tool_scope, "library_actions() declares no tool action: a scope is closed because the library DEFINES it, not because it SHIPS the tool [" + named + "]");
    // The same rule for the file that ships beside it: it belongs to every host, so a
    // `studio.quit` row in it would be a key every host advertises and cannot press.
    // (default_bindings() aborts on this; asserted here so the failure has a name.)
    BindingsLoadReport srep;
    RolltuiBindings* shipped = bindings_from_json(default_bindings_json(), srep);
    std::string stray;
    for (const std::string& a : bindings_undeclared(shipped))
      if (scope_of(a) != "app") stray += " " + a;
    check(shipped && stray.empty(), "the shipped bindings file binds the library's widgets and the shipped screen's app.* and nothing else —" + (stray.empty() ? " none" : stray));
    rolltui_bindings_free(shipped);

    // A PHASE 10 BINDINGS FILE still loads clean and keeps its rows. This is the mercy
    // the whole split depends on, and which side of the table a scope sits on is what
    // decides it: a typo in a LIBRARY scope is an unknown action, while `studio.quit`
    // — now in nobody's closed set — is kept, inert, until something declares it.
    const char* phase10 = R"({"name":"p10","bindings":{"input.submit":["enter"],"app.help":["f1"],
        "editor.undo":["ctrl+z"],"studio.quit":["ctrl+q"],"studio.reload":["f5"]}})";
    BindingsLoadReport prep;
    RolltuiBindings* p = bindings_from_json(phase10, prep);
    check(p && prep.clean(), "a bindings file binding the unmounted studio.quit loads CLEAN [" + prep.summary() + "]");
    check(bindings_chords_for(p, "studio.quit").size() == 1 && !bindings_has(p, "studio.quit") && bindings_action_for(p, ch('q', true), "studio").empty(),
          "…its row is kept and inert: nothing has mounted the studio, so nothing emits it");
    BindingsLoadReport rt;
    RolltuiBindings* back = bindings_from_json(bindings_to_json(p, "p10"), rt);
    check(back && rt.clean() && bindings_equal(back, p), "…and it survives the round trip, so `bindings save` never loses another program's keys");
    rolltui_bindings_free(back);

    // MOUNTING the tool: one authoritative declare() takes the layout's actions and the
    // tool's, and the tool's suggested chord fills only a GAP.
    const std::vector<ToolAction> tool = {{"studio.quit", "quit", "ctrl+q"},
                                          {"studio.reload", "reload the fixture", "f5"},
                                          {"studio.cycle_theme", "cycle the shipped theme presets", "f3"}};
    bindings_declare(p, {{"app.help", "open help"}}, tool);
    check(bindings_has(p, "studio.quit") && bindings_action_for(p, ch('q', true), "studio") == "studio.quit" && bindings_has(p, "app.help"),
          "declaring the layout's actions and the mounted tool's in ONE call makes both live");
    check(bindings_chords_for(p, "studio.cycle_theme").size() == 1 && bindings_action_for(p, key(static_cast<unsigned char>(ROLLTUI_KEY_F1 + 2)), "studio") == "studio.cycle_theme",
          "…an action the file never named gets the tool's suggested chord (the gap it is for)");
    check(bindings_chords_for(p, "studio.quit").size() == 1 && bindings_chords_for(p, "studio.reload").size() == 1,
          "…and one it did named keeps exactly the file's row: a suggestion never overrides");
    // The order trap this rule was first got wrong on: a declaration creates an empty row
    // for its action, so a suggestion made AFTER one would decline every time and every
    // tool key would be silently unbound. One call, one order.
    RolltuiBindings* fresh = bindings_new();
    bindings_declare(fresh, {}, tool);
    check(bindings_action_for(fresh, ch('q', true), "studio") == "studio.quit" && bindings_action_for(fresh, key(static_cast<unsigned char>(ROLLTUI_KEY_F1 + 4)), "studio") == "studio.reload",
          "a mounted tool's keys work on a table that had never heard of it");
    rolltui_bindings_free(fresh);

    // The three ways a suggestion is DECLINED, all leaving the action declared-and-unbound
    // rather than absent or sharing a chord.
    BindingsLoadReport urep;
    RolltuiBindings* unbound = bindings_from_json(R"({"name":"u","bindings":{"studio.quit":[],"studio.reload":["ctrl+q"]}})", urep);
    bindings_declare(unbound, {}, tool);
    check(bindings_has(unbound, "studio.quit") && bindings_chords_for(unbound, "studio.quit").empty(),
          "an EMPTY row wins too — a file (or a user) said 'unbound', and a suggestion must not bring the key back");
    check(bindings_action_for(unbound, ch('q', true), "studio") == "studio.reload", "…and the chord the file moved stays where the file put it");
    rolltui_bindings_free(unbound);
    RolltuiBindings* clash = bindings_new();
    bindings_declare(clash, {}, {{"mine.one", "one", "f9"}, {"mine.two", "two", "f9"}, {"mine.three", "three", "not+a+chord"}});
    check(bindings_action_for(clash, key(static_cast<unsigned char>(ROLLTUI_KEY_F1 + 8)), "mine") == "mine.one" && bindings_has(clash, "mine.two") && bindings_chords_for(clash, "mine.two").empty(),
          "a suggestion whose chord already serves the scope is declined: the action is declared UNBOUND, not a second holder of one chord");
    check(bindings_has(clash, "mine.three") && bindings_chords_for(clash, "mine.three").empty(), "…and an unparseable chord is the same: visible as (unbound), never missing");
    rolltui_bindings_free(clash);
    // Authoritative still: the tools survive a screen change because they are passed
    // every time; the last screen's app actions do not.
    bindings_declare(p, {{"app.zoom", "zoom in"}}, tool);
    check(!bindings_has(p, "app.help") && bindings_has(p, "app.zoom") && bindings_action_for(p, ch('q', true), "studio") == "studio.quit",
          "a new screen replaces the layout's actions and keeps the mounted tool's");
    bindings_declare(p, {{"app.zoom", "zoom in"}}, {});
    check(!bindings_has(p, "studio.quit") && bindings_chords_for(p, "studio.quit").size() == 1,
          "…and UNmounting the tool makes its actions inert again, chords kept: nothing else can advertise them");
    rolltui_bindings_free(p);
  }
  // ---- Phase 12 m3: a chord this terminal cannot deliver is REFUSED, not bound to
  // silence. The MODEL (which chord, under which protocol, and why) is measured against
  // the encodings in deliverability_test; what belongs here is the LOADER's contract —
  // that the refusal is a named problem of its own kind, that the row survives it, and
  // that the two mercy rungs above are not quietly re-implemented as refusals.
  {
    rolltui_key_set_active_protocol(ROLLTUI_PROTOCOL_LEGACY);  // no Terminal here, so say it rather than inherit it
    BindingsLoadReport rep;
    RolltuiBindings* b = bindings_from_json(
        R"({"name":"x","bindings":{"input.submit":["enter"],"app.palette":["ctrl+shift+p","ctrl+p"],"app.help":["f1"]}})",
        rep, ROLLTUI_PROTOCOL_LEGACY);
    check(b && rep.undeliverable.size() == 1 && rep.bad_chords.empty() && rep.conflicts.empty(),
          "an undeliverable chord is its OWN kind of problem: it parses, it conflicts with nothing, it cannot arrive");
    check(b && !rep.clean() && rep.summary().find("undeliverable: app.palette: 'ctrl+shift+p'") != std::string::npos,
          "…it makes the file unclean and the summary says which action and which chord [" + rep.summary() + "]");
    check(b && bindings_chords_for(b, "app.palette").size() == 2,
          "…and BOTH chords are kept: the file is the user's, and the terminal it was written on is not this one");
    bindings_declare(b, {{"app.palette", "the palette"}});
    check(bindings_action_for(b, ch(U'p', true), "app") == "app.palette" && bindings_chords_text(b, "app.palette") == "Ctrl-P",
          "…so the deliverable chord still works and the help shows only it [" + bindings_chords_text(b, "app.palette") + "]");
    rolltui_bindings_free(b);

    // The shipped file, against the WEAKEST protocol — the same check default_bindings()
    // aborts on, asserted here so the failure has a name and not only an exit status
    // (Phase 11 m1's tool-row abort, same shape).
    BindingsLoadReport srep;
    RolltuiBindings* shipped = bindings_from_json(default_bindings_json(), srep, ROLLTUI_PROTOCOL_LEGACY);
    std::string named;
    for (const std::string& u : srep.undeliverable) named += " " + u;
    check(shipped && srep.undeliverable.empty(),
          "the shipped bindings file is deliverable on every terminal, not just this one —" +
              (named.empty() ? std::string(" none") : named));
    rolltui_bindings_free(shipped);

    // The mercy rung may not become an undeliverability refusal by accident: another
    // screen's action is kept (m1) under a protocol that refuses one of the chords in the
    // same file.
    BindingsLoadReport mrep;
    RolltuiBindings* m = bindings_from_json(
        R"({"name":"m","bindings":{"input.submit":["enter"],"other.thing":["f9"],"studio.quit":["ctrl+q"],
            "app.zoom":["ctrl+shift+z"]}})",
        mrep, ROLLTUI_PROTOCOL_LEGACY);
    check(m && mrep.undeliverable.size() == 1 && bindings_chords_for(m, "studio.quit").size() == 1 &&
              bindings_chords_for(m, "other.thing").size() == 1 && bindings_chords_for(m, "app.zoom").size() == 1,
          "kept-and-inert is untouched by deliverability — all three rows survive");
    rolltui_bindings_free(m);
  }
  // ---- the loader's report ----
  {
    BindingsLoadReport rep;
    RolltuiBindings* b = bindings_from_json(R"({"name":"x","bindings":{"input.left":["left","ctrl+b"],"input.right":["left"],"input.nothing":["x"],"input.up":["meta+hyper+z"],"input.newline":["enter"],"input.submit":["ctrl+j"]},"extra":1})", rep);
    check(b && rep.error.empty(), "a file with problems still loads");
    check(rep.conflicts.size() == 1 && rep.conflicts[0].find("'left' bound to both input.left and input.right") == 0 && bindings_action_for(b, key(ROLLTUI_KEY_LEFT), "input") == "input.left",
          "a chord bound twice in one scope is a conflict; the first binding wins [" + (rep.conflicts.empty() ? "" : rep.conflicts[0]) + "]");
    check(rep.unknown_actions == std::vector<std::string>{"input.nothing"}, "an unknown action is reported by name");
    check(rep.bad_chords.size() == 1 && rep.bad_chords[0].find("input.up: 'meta+hyper+z'") == 0, "an unparseable chord is reported with its action");
    check(rep.bad_values.size() == 2 && rep.bad_values[0].find("input.newline: 'enter' is always input.submit") == 0 && rep.bad_values[1].find("input.submit: 'enter' is always bound") == 0,
          "the Enter rule: binding Enter elsewhere in the input scope is refused by name, and input.submit gets Enter back");
    check(bindings_action_for(b, key(ROLLTUI_KEY_ENTER), "input") == "input.submit" && bindings_action_for(b, ch('j', true), "input") == "input.submit", "…so Enter submits, and ctrl+j too");
    check(rep.unknown_keys == std::vector<std::string>{"extra"}, "an unknown top-level key is reported");
    check(!bindings_from_json("[1]", rep) && !rep.error.empty(), "a non-object is unusable");
    check(!bindings_from_json(R"({"name":"x"})", rep) && rep.error.find("bindings") != std::string::npos, "a file without a bindings object is unusable");
    RolltuiBindings* empty = bindings_from_json(R"({"name":"e","bindings":{}})", rep);
    check(empty && bindings_chords_for(empty, "input.left").empty() && !bindings_chords_for(empty, "input.submit").empty(), "an empty file binds nothing but Enter → submit (a file is the whole domain)");
    rolltui_bindings_free(empty);
    rolltui_bindings_free(b);
  }
  // ---- bind / unbind ----
  {
    RolltuiBindings* b = default_bindings();
    std::string moved;
    check(bindings_bind(b, "input.word_left", *parse_chord("alt+b"), &moved) && moved.empty() && bindings_action_for(b, ch('b', false, true), "input") == "input.word_left", "bind adds a chord");
    check(bindings_bind(b, "input.word_right", *parse_chord("alt+d"), &moved) && moved == "input.kill_word_forward" && bindings_action_for(b, ch('d', false, true), "input") == "input.word_right" &&
              bindings_action_for(b, key(ROLLTUI_KEY_DELETE, true), "input") == "input.kill_word_forward",
          "a chord bound elsewhere in the scope moves, and moved_from names the loser");
    check(!bindings_bind(b, "input.newline", key(ROLLTUI_KEY_ENTER)) && bindings_action_for(b, key(ROLLTUI_KEY_ENTER), "input") == "input.submit", "Enter cannot be bound to another input action");
    check(!bindings_bind(b, "input.nope", key(static_cast<unsigned char>(ROLLTUI_KEY_F1 + 8))), "an unknown action is refused");
    check(bindings_bind(b, "transcript.top", key(ROLLTUI_KEY_ENTER)) && bindings_action_for(b, key(ROLLTUI_KEY_ENTER), "transcript") == "transcript.top", "…but Enter may serve another scope");
    check(!bindings_unbind(b, "input.submit", key(ROLLTUI_KEY_ENTER)) && bindings_unbind(b, "input.word_left", *parse_chord("alt+b")) && !bindings_unbind(b, "input.word_left", *parse_chord("alt+b")), "unbind: Enter stays on submit; a chord removes once");
    bindings_clear(b, "input.copy");
    check(bindings_chords_for(b, "input.copy").empty(), "clear empties an action");
    bindings_clear(b, "input.submit");
    check(!bindings_chords_for(b, "input.submit").empty(), "…except input.submit");
    bindings_add_action(b, "mine.thing", "my host's own");
    check(bindings_bind(b, "mine.thing", key(static_cast<unsigned char>(ROLLTUI_KEY_F1 + 8))) && bindings_action_for(b, key(static_cast<unsigned char>(ROLLTUI_KEY_F1 + 8)), "mine") == "mine.thing" && bindings_description(b, "mine.thing") == "my host's own", "a host may add its own actions");
    rolltui_bindings_free(b);
  }
  // ---- round trip ----
  {
    RolltuiBindings* d = default_bindings();
    BindingsLoadReport rep;
    RolltuiBindings* back = bindings_from_json(bindings_to_json(d, "default"), rep);
    check(back && rep.clean() && bindings_equal(back, d), "to_json / from_json round-trips the default exactly");
    rolltui_bindings_free(back);
    RolltuiBindings* edited = rolltui_bindings_clone(d);
    bindings_bind(edited, "input.word_left", *parse_chord("alt+b"));
    check(!bindings_equal(edited, d), "an edit makes the tables unequal (the label-by-comparison rule can stand on this)");
    rolltui_bindings_free(edited);
    rolltui_bindings_free(d);
  }
  // ---- help ----
  {
    RolltuiBindings* d1 = default_bindings();
    const std::vector<std::string> lines = help_lines(d1, "input", {"input.submit", "input.kill_word_backward"});
    check(lines.size() == 2 && lines[0].find("Enter") == 0 && lines[0].find("send the line") != std::string::npos && lines[1].find("Ctrl-W, Alt-Backspace") == 0,
          "help_lines: the chords, then the description [" + (lines.empty() ? "" : lines[0]) + "]");
    rolltui_bindings_free(d1);
    RolltuiBindings* d2 = default_bindings();
    const std::vector<std::string> all = help_lines(d2, "menu");
    check(all.size() == 11 && all[0].find("Up") == 0, "an empty list means every action of the scope (menu: 11)");
    rolltui_bindings_free(d2);
    RolltuiBindings* vim = default_bindings();
    bindings_bind(vim, "input.word_left", *parse_chord("alt+b"));
    check(help_lines(vim, "input", {"input.word_left"})[0].find("Alt-B") != std::string::npos, "help follows a rebinding: it is rendered from the live table");
    rolltui_bindings_free(vim);
    // The chord column is capped at 24 cells: one long chord list does not push every
    // other description across a narrow popup (found by a 46-column golden).
    RolltuiBindings* wide = default_bindings();
    bindings_bind(wide, "input.word_right", *parse_chord("ctrl+shift+f12"));
    bindings_bind(wide, "input.word_right", *parse_chord("alt+shift+f11"));
    const std::vector<std::string> capped = help_lines(wide, "input", {"input.left", "input.word_right"});
    check(capped[0].find("move one grapheme left") <= 24, "a short chord's description starts within the 24-cell column (at " + std::to_string(capped[0].find("move one grapheme left")) + ")");
    check(capped[1].find("  move one word right") != std::string::npos && capped[1].find("move one word right") > 24,
          "a chord list longer than the column is followed by two spaces, not padded");
    rolltui_bindings_free(wide);
  }
  // ---- the four per-widget tables, expanded from the one list (Phase 17 m2a) ------------
  // `rolltui_library_actions.c` builds `RolltuiInputActions`, `RolltuiMenuActions`,
  // `RolltuiTranscriptActions` and `RolltuiScrollTextActions` by expanding the 59-row list with
  // DESIGNATED initialisers, so a row whose group or field is wrong leaves that member NULL
  // rather than failing to compile — and a NULL action name reads as "nothing is bound to this
  // command", which is silent. This is the check that makes the expansion self-verifying: every
  // member of all four is non-NULL, and every one names an action the table actually declares.
  {
    RolltuiBindings* d = default_bindings();
    const RolltuiInputActions* ia = rolltui_input_default_actions();
    const RolltuiMenuActions* ma = rolltui_menu_default_actions();
    const RolltuiTranscriptActions* ta = rolltui_transcript_default_actions();
    const RolltuiScrollTextActions* sa = rolltui_scroll_text_default_actions();
    const char* const* input_fields = reinterpret_cast<const char* const*>(ia);
    const std::size_t input_n = sizeof(RolltuiInputActions) / sizeof(const char*);
    const char* const* trans_fields = reinterpret_cast<const char* const*>(ta);
    const std::size_t trans_n = sizeof(RolltuiTranscriptActions) / sizeof(const char*);
    const char* const* scroll_fields = reinterpret_cast<const char* const*>(sa);
    const std::size_t scroll_n = sizeof(RolltuiScrollTextActions) / sizeof(const char*);
    // The menu's last member is the input POINTER, not a name, so it stops one short.
    const char* const* menu_fields = reinterpret_cast<const char* const*>(ma);
    const std::size_t menu_n = sizeof(RolltuiMenuActions) / sizeof(const char*) - 1;
    int missing = 0, undeclared = 0;
    std::string bad;
    auto scan = [&](const char* const* f, std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        if (!f[i]) { ++missing; continue; }
        if (!rolltui_bindings_has(d, f[i], std::strlen(f[i]))) { ++undeclared; bad += std::string(" ") + f[i]; }
      }
    };
    scan(input_fields, input_n);
    scan(menu_fields, menu_n);
    scan(trans_fields, trans_n);
    scan(scroll_fields, scroll_n);
    check(input_n == 30 && menu_n == 15 && trans_n == 11 && scroll_n == 6,
          "the four tables are the sizes the headers declare [" + std::to_string(input_n) + "/" +
              std::to_string(menu_n) + "/" + std::to_string(trans_n) + "/" + std::to_string(scroll_n) + "]");
    check(missing == 0, "…every member is filled by the expansion [" + std::to_string(missing) + " NULL]");
    check(undeclared == 0, "…and every one names an action the library declares [" + bad + "]");
    check(ma->input == ia, "…and the menu's input table IS the input table, not a copy of it");
  }

  return report("rolltui bindings_test");
}
