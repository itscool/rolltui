//
// deliverability_test.cpp — a chord a terminal cannot deliver is REFUSED, not bound to
// silence (Phase 12 m3).
//
// THE DEFECT: `ctrl+shift+p` parses, binds, saves and renders in the help popup, and on
// an ordinary terminal the bytes that arrive are byte-identical to `ctrl+p`, so it never
// fires. Correct in every observable way except that it does nothing.
//
// WHAT MAKES THIS A TEST RATHER THAN A RESTATEMENT. Keys.cpp holds the same fact twice,
// deliberately: `encode_key` says what a terminal SENDS for a chord (the encodings,
// every rule of them cited), and `deliverable` is a cheap classification a key press can
// afford. Neither is checked by the other's existence. So the first block below
// enumerates ~1000 chords × 3 protocols and asserts, for every one of them, that the
// RULE agrees with feeding the ENCODING to the real KeyDecoder — a row where the model
// and the bytes disagree fails by name. A deliverability table asserted against the
// model that produced it would prove nothing at all.
//
// SOURCES, once, since the individual rows below cite them by short name:
//   [kitty]  sw.kovidgoyal.net/kitty/keyboard-protocol — the progressive-enhancement
//            flags, `CSI code ; mod u` with the code ALWAYS the unshifted key, the
//            legacy functional-key table, and the "Problems with the legacy keyboard
//            protocol" list ("No way to reliably use multiple modifier keys, other
//            than, shift+alt and ctrl+alt").
//   [xterm]  invisible-island.net/xterm/modified-keys.html and ctlseqs.html —
//            modifyOtherKeys (`CSI > 4 ; Ps m`), the `CSI 27 ; modifier ; keycode ~`
//            form with its worked examples, the modifier parameter 1 + (shift 1 |
//            alt 2 | ctrl 4), and the cursor/function keys that already carry it.
//
// PHASE 17: calls the C API (rolltui/c/rolltui_keys.h, rolltui_bindings.h,
// rolltui_terminal.h, all reached through rolltui/rolltui.h) directly rather than through
// the rolltui::KeyEvent / KeyDecoder / Bindings / Terminal C++ shims (Keys.hpp + its shim
// Keys.cpp, Bindings.hpp + its shim Bindings.cpp, Terminal.hpp + its shim Terminal.cpp)
// this file used to include — those are the files being deleted. There is no KeyEvent on
// this side of the boundary (rolltui_keys.h's note: it carried a std::string only a C++
// type could hold), so every chord here is a RolltuiChord built directly. `KeyProtocol`'s
// NAMES ("legacy"/"modifyOtherKeys"/"kitty") and the four undeliverability sentences have
// no C form at all — Keys.cpp keeps that vocabulary out of the C layer on purpose, for a
// config file and a `--keys` flag to spell — so this file re-states them locally, tied to
// the C's own ordinals/codes so neither can drift. `library_actions()` and
// `migrated_action()` are the same kind of vocabulary one level up (Bindings.cpp keeps
// them out of rolltui_bindings.h for the same reason), so this file copies that table too
// — the same trade tests/input_test.cpp already made for its own action table.
//
#include <array>
#include "rolltui/c/rolltui_embedded.h"
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "rolltui/rolltui.h"

#include "rolltui_test.hpp"

// The shipped bindings preset text, generated at build time and linked into `rolltui`
// (rolltui/cmake/embed_presets.cmake); Bindings.cpp reads it through this exact
// declaration, and default_bindings_json() below does the same rather than duplicating
// the embedding.

using namespace rolltui_test;

namespace {

// KeyProtocol's ORDINALS cross (rolltui_keys.h's ROLLTUI_PROTOCOL_*); its NAMES do not.
// This local enum is tied to those ordinals so neither can drift from what the C
// classifies.
enum class KeyProtocol : unsigned char {
  Legacy = ROLLTUI_PROTOCOL_LEGACY,
  ModifyOtherKeys = ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS,
  Kitty = ROLLTUI_PROTOCOL_KITTY,
};
std::string_view protocol_name(KeyProtocol p) {
  switch (p) {
    case KeyProtocol::Kitty: return "kitty";
    case KeyProtocol::ModifyOtherKeys: return "modifyOtherKeys";
    case KeyProtocol::Legacy: break;
  }
  return "legacy";
}
void set_active_key_protocol(KeyProtocol p) { rolltui_key_set_active_protocol(static_cast<unsigned char>(p)); }

bool deliverable(const RolltuiChord& k, KeyProtocol p) {
  return rolltui_key_deliverable(&k, static_cast<unsigned char>(p)) != 0;
}
std::optional<std::string> encode_key(const RolltuiChord& k, KeyProtocol p) {
  // CALLER-FILLED, with the bound known WITHOUT asking: the longest encoding any protocol
  // produces is a constant in the header, so there is no measure-then-fill round trip.
  char buf[ROLLTUI_KEY_ENCODE_MAX];
  const long n = rolltui_key_encode(&k, static_cast<unsigned char>(p), buf, sizeof buf);
  if (n < 0) return std::nullopt;
  return std::string(buf, static_cast<std::size_t>(n));
}
// THE WORDS, against the C's classification (Keys.cpp: "the C classifies and never
// carries a sentence"). The reason names the CHEAPEST protocol that would carry the
// chord, so a person is told what to turn on rather than that something is impossible.
std::string undeliverable_reason(const RolltuiChord& k, KeyProtocol p) {
  static constexpr std::array<std::string_view, 6> kReasons = {
      "",
      "it is not a key",
      "shift on a character key is the shifted character itself, which no terminal reports as a chord",
      "it needs the kitty keyboard protocol or xterm's modifyOtherKeys",
      "it needs the kitty keyboard protocol",
      "no keyboard protocol this library speaks can report it",
  };
  const int code = rolltui_key_undeliverable_reason(&k, static_cast<unsigned char>(p));
  return std::string(kReasons[static_cast<std::size_t>(code) < kReasons.size() ? static_cast<std::size_t>(code) : 0]);
}
std::string show(const RolltuiChord& k) {
  char buf[ROLLTUI_CHORD_STRING_MAX];
  return std::string(buf, rolltui_chord_to_string(&k, buf, sizeof buf));
}
// What a KeyEvent's defaulted operator== compared once `raw` was cleared — the chord
// alone, which is all a RolltuiChord ever carries.
bool chord_eq(const RolltuiChord& a, const RolltuiChord& b) {
  return a.key == b.key && a.ch == b.ch && a.ctrl == b.ctrl && a.alt == b.alt && a.shift == b.shift;
}

// F2..F11 have no named constant of their own (only F1 and F12 are, rolltui_keys.h),
// because rolltui::Key numbered them contiguously and the C only had to pin the two ends.
constexpr int kF1 = ROLLTUI_KEY_F1, kF2 = kF1 + 1, kF3 = kF1 + 2, kF4 = kF1 + 3, kF5 = kF1 + 4, kF6 = kF1 + 5,
              kF7 = kF1 + 6, kF8 = kF1 + 7, kF9 = kF1 + 8, kF10 = kF1 + 9, kF11 = kF1 + 10, kF12 = kF1 + 11;
static_assert(kF12 == ROLLTUI_KEY_F12, "F1..F12 must be contiguous, matching rolltui::Key's declared order");

RolltuiChord key(int k, bool ctrl = false, bool alt = false, bool shift = false) {
  RolltuiChord c{};
  c.key = static_cast<unsigned char>(k);
  c.ctrl = ctrl;
  c.alt = alt;
  c.shift = shift;
  return c;
}
RolltuiChord ch(char32_t cp, bool ctrl = false, bool alt = false, bool shift = false) {
  RolltuiChord c{};
  c.key = ROLLTUI_KEY_CHAR;
  c.ch = cp;
  c.ctrl = ctrl;
  c.alt = alt;
  c.shift = shift;
  return c;
}

// What this library ACTUALLY makes of those bytes. flush() is part of it: a lone ESC is
// only the Escape key once nothing follows, which is exactly what Terminal::poll does on
// a timeout. A decoded event's raw bytes are never inspected below (an Unknown key never
// arrives from an encoding this file produced itself), so only the kind and the chord are
// kept — there is no KeyEvent on this side of the boundary to hold anything else in.
struct DecodedEvent {
  unsigned char kind = ROLLTUI_EVENT_KEY;
  RolltuiChord key{};
};
void collect(void* ctx, const RolltuiEvent* e) {
  std::vector<DecodedEvent>& out = *static_cast<std::vector<DecodedEvent>*>(ctx);
  out.push_back({e->kind, e->key});
}
using DecoderPtr = std::unique_ptr<RolltuiKeyDecoder, void (*)(RolltuiKeyDecoder*)>;
std::vector<DecodedEvent> decode(const std::string& bytes) {
  DecoderPtr d(rolltui_key_decoder_new(), rolltui_key_decoder_free);
  std::vector<DecodedEvent> ev;
  rolltui_key_decoder_feed(d.get(), bytes.data(), bytes.size(), collect, &ev);
  rolltui_key_decoder_flush(d.get(), collect, &ev);
  return ev;
}

// The round trip: does the protocol's encoding of this chord come back as this chord?
// This is the ground truth the rule is measured against — not a second copy of the rule.
bool arrives_as_itself(const RolltuiChord& k, KeyProtocol p) {
  const std::optional<std::string> bytes = encode_key(k, p);
  if (!bytes || bytes->empty()) return false;
  const std::vector<DecodedEvent> ev = decode(*bytes);
  if (ev.size() != 1) return false;
  if (ev[0].kind != ROLLTUI_EVENT_KEY) return false;
  return chord_eq(ev[0].key, k);
}

std::string show_bytes(const std::optional<std::string>& b) {
  if (!b) return "(no encoding)";
  std::string s;
  for (char c : *b) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u == 0x1b) s += "ESC";
    else if (u < 0x20 || u == 0x7f) s += "^" + std::string(1, static_cast<char>(u ^ 0x40));
    else s += static_cast<char>(u);
  }
  return s;
}

// Every chord worth enumerating: each named key and a wide spread of characters, under
// all eight modifier combinations.
std::vector<RolltuiChord> universe() {
  std::vector<RolltuiChord> all;
  static const int kKeys[] = {ROLLTUI_KEY_ENTER,  ROLLTUI_KEY_TAB,     ROLLTUI_KEY_BACKSPACE, ROLLTUI_KEY_ESCAPE,
                              ROLLTUI_KEY_UP,      ROLLTUI_KEY_DOWN,    ROLLTUI_KEY_LEFT,      ROLLTUI_KEY_RIGHT,
                              ROLLTUI_KEY_HOME,    ROLLTUI_KEY_END,     ROLLTUI_KEY_PAGEUP,    ROLLTUI_KEY_PAGEDOWN,
                              ROLLTUI_KEY_INSERT,  ROLLTUI_KEY_DELETE,  kF1,                   kF2,
                              kF3,                 kF4,                 kF5,                   kF6,
                              kF7,                 kF8,                 kF9,                   kF10,
                              kF11,                kF12};
  const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789 !@#$%^&*()-_=+[]{}\\|;:'\",.<>/?`~";
  for (int m = 0; m < 8; ++m) {
    const bool sh = m & 1, al = m & 2, ct = m & 4;
    for (int k : kKeys) all.push_back(key(k, ct, al, sh));
    for (char c : chars) all.push_back(ch(static_cast<char32_t>(c), ct, al, sh));
  }
  return all;
}

// ---- the pty half: the answer is the TERMINAL's ------------------------------------
// A real pty pair with this test acting as the terminal on the master side. The child
// side is a Terminal; we read its queries and reply — or do not — and assert what it
// negotiated. This is the only thing that can show the model is not a hardcoded table:
// the same library, the same file, two different answers, decided by the replies.

struct Pty {
  int master = -1, slave = -1;
  bool open() {
    winsize ws{};
    ws.ws_row = 24;
    ws.ws_col = 80;
    return openpty(&master, &slave, nullptr, nullptr, &ws) == 0;
  }
  void close_all() {
    if (master >= 0) ::close(master);
    if (slave >= 0) ::close(slave);
  }
  // Waits for the child's Primary DA query, then writes `reply` (the protocol answer,
  // if any) followed by a DA response — the terminator that makes a negative answer
  // definitive rather than a timeout.
  void answer(const std::string& reply) {
    std::string got;
    for (int waited = 0; waited < 400 && got.find("\x1b[c") == std::string::npos; waited += 10) {
      pollfd p{master, POLLIN, 0};
      if (::poll(&p, 1, 10) > 0 && (p.revents & POLLIN)) {
        char buf[512];
        const ssize_t k = ::read(master, buf, sizeof buf);
        if (k > 0) got.append(buf, static_cast<std::size_t>(k));
      }
    }
    const std::string all = reply + "\x1b[?62;1;2c";
    (void)!::write(master, all.data(), all.size());
  }
};

// Runs a Terminal on a pty whose master answers `reply`, and returns what it negotiated.
// The reply has to be written from another process, because the Terminal's own
// negotiation blocks: fork, let the child be the terminal, be the application here.
KeyProtocol negotiated_with(const std::string& reply) {
  Pty pty;
  if (!pty.open()) return KeyProtocol::Legacy;
  const pid_t pid = ::fork();
  if (pid == 0) {  // the TERMINAL side
    ::close(pty.slave);
    pty.answer(reply);
    ::usleep(50 * 1000);
    ::_exit(0);
  }
  ::close(pty.master);
  KeyProtocol p = KeyProtocol::Legacy;
  {
    RolltuiTerminalOptions opts{};
    opts.handle_signals = false;  // this is a test process, not an application
    RolltuiTerminal* t = rolltui_terminal_new(pty.slave, pty.slave, opts);
    p = static_cast<KeyProtocol>(rolltui_terminal_key_protocol(t));
    rolltui_terminal_free(t);  // restores the terminal, exactly as ~Terminal did
  }
  ::close(pty.slave);
  int status = 0;
  ::waitpid(pid, &status, 0);
  return p;
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
// THE MIGRATION TABLE, copied verbatim from Bindings.cpp — the one place any source
// still carries the old `rolltui-playground` action names.
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

int is_library_scope_cb(void*, const char* scope, std::size_t len) {
  return library_scope(std::string_view(scope, len)) ? 1 : 0;
}
int migrate_cb(void*, const char* legacy, std::size_t len, char* out, std::size_t* out_len) {
  const std::optional<std::string> to = migrated_action(std::string_view(legacy, len));
  if (!to) return 0;
  const std::size_t n = std::min(to->size(), static_cast<std::size_t>(ROLLTUI_ACTION_NAME_MAX));
  std::memcpy(out, to->data(), n);
  *out_len = n;
  return 1;
}
std::size_t reason_cb(void*, const RolltuiChord* k, unsigned char protocol, char* out, std::size_t cap) {
  const std::string r = undeliverable_reason(*k, static_cast<KeyProtocol>(protocol));
  const std::size_t n = std::min(r.size(), cap);
  std::memcpy(out, r.data(), n);
  return n;
}

struct BindingsLoadReport {
  std::string error;
  std::vector<std::string> unknown_actions;
  std::vector<std::string> bad_chords;
  std::vector<std::string> undeliverable;
  std::vector<std::string> conflicts;
  std::vector<std::string> bad_values;
  std::vector<std::string> unknown_keys;
  std::vector<std::string> migrated;
  bool clean() const {
    return error.empty() && unknown_actions.empty() && bad_chords.empty() && conflicts.empty() && bad_values.empty() &&
           unknown_keys.empty() && undeliverable.empty();
  }
  std::string summary() const {
    if (clean()) return "";
    if (!error.empty()) return error;
    std::string s;
    auto add = [&](const std::string& x) {
      if (!s.empty()) s += "; ";
      s += x;
    };
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

// ---- the table: OWNED, an explicit new/free pair. NULL (rather than std::nullopt) is
// the failure state a caller checks — a unique_ptr already has one. ----
using BindingsPtr = std::unique_ptr<RolltuiBindings, void (*)(RolltuiBindings*)>;
constexpr std::string_view kEnterAction = "input.submit";
BindingsPtr new_bindings() {
  BindingsPtr b(rolltui_bindings_new(), rolltui_bindings_free);
  rolltui_bindings_set_enter_rule(b.get(), kEnterAction.data(), kEnterAction.size());
  for (const ActionInfo& a : library_actions())
    rolltui_bindings_add_action(b.get(), a.name.data(), a.name.size(), a.description.data(), a.description.size());
  return b;
}
BindingsPtr bindings_from_json(std::string_view text, BindingsLoadReport& report, KeyProtocol deliver) {
  BindingsPtr b = new_bindings();  // seeded with library_actions(), exactly as Bindings() did
  RolltuiBindingsReport rep{};
  const int ok = rolltui_bindings_load_json(b.get(), text.data(), text.size(), static_cast<unsigned char>(deliver),
                                            is_library_scope_cb, nullptr, migrate_cb, nullptr, reason_cb, nullptr,
                                            &rep);
  copy_report(report, rep);
  rolltui_bindings_report_release(&rep);
  if (!ok) b.reset();
  return b;
}
std::string bindings_to_json(const RolltuiBindings* b, std::string_view name) {
  RolltuiStr text;
  rolltui_bindings_dump_json(b, name.data(), name.size(), &text);
  return text.str();
}
std::vector<RolltuiChord> chords_for(const RolltuiBindings* b, std::string_view action) {
  const std::size_t n = rolltui_bindings_chord_count(b, action.data(), action.size());
  std::vector<RolltuiChord> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord c;
    if (rolltui_bindings_chord_at(b, action.data(), action.size(), i, &c)) out.push_back(c);
  }
  return out;
}
// The HELP form: skips what this terminal cannot deliver (Bindings.hpp's "inert also
// means invisible"). Reads through count-plus-index, allocating only the string returned.
std::string chords_text(const RolltuiBindings* b, std::string_view action) {
  const unsigned char p = rolltui_key_active_protocol();
  const std::size_t n = rolltui_bindings_chord_count(b, action.data(), action.size());
  std::string s;
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord c;
    if (!rolltui_bindings_chord_at(b, action.data(), action.size(), i, &c)) continue;
    if (!rolltui_key_deliverable(&c, p)) continue;
    char buf[ROLLTUI_CHORD_STRING_MAX];
    const std::size_t len = rolltui_chord_display(&c, buf, sizeof buf);
    if (!s.empty()) s += ", ";
    s.append(buf, len);
  }
  return s;
}
std::string_view action_for(const RolltuiBindings* b, const RolltuiChord& k, std::string_view scope) {
  std::size_t len = 0;
  const char* a = rolltui_bindings_action_for(b, &k, scope.data(), scope.size(), &len);
  return a ? std::string_view(a, len) : std::string_view();
}
struct ActionDecl {
  std::string name, description;
};
// declare(), simplified to the one shape this file uses (no mounted tools — `suggest()`
// is a no-op over an empty tool list, so the only half that matters here is
// undeclare-others-then-add).
void declare(RolltuiBindings* b, const std::vector<ActionDecl>& declared) {
  rolltui_bindings_undeclare_others(b, is_library_scope_cb, nullptr);
  for (const ActionDecl& d : declared)
    rolltui_bindings_add_action(b, d.name.data(), d.name.size(), d.description.data(), d.description.size());
}

std::string_view default_bindings_json() {
  for (std::size_t i = 0; i < rolltui_kBindingsPresetCount; ++i)
    if (std::string_view(rolltui_kBindingsPresets[i].name) == "default") return rolltui_kBindingsPresets[i].text;
  return "";
}

}  // namespace

int main() {
  // ---- 1. THE TABLE, AGAINST THE ENCODINGS ------------------------------------------
  // The rule and the bytes, over the whole universe. Any disagreement names the chord,
  // the protocol, the bytes and both verdicts.
  {
    const KeyProtocol protocols[] = {KeyProtocol::Legacy, KeyProtocol::ModifyOtherKeys, KeyProtocol::Kitty};
    int checked = 0;
    for (const RolltuiChord& k : universe())
      for (KeyProtocol p : protocols) {
        const bool rule = deliverable(k, p);
        const bool bytes = arrives_as_itself(k, p);
        ++checked;
        check_quiet(rule == bytes, "deliverable('" + show(k) + "', " + std::string(protocol_name(p)) + ") = " +
                                       (rule ? "yes" : "no") + " but " + show_bytes(encode_key(k, p)) + " decodes to " +
                                       (bytes ? "itself" : "something else"));
      }
    check(checked > 2000, "the rule was measured against the encodings for " + std::to_string(checked) +
                              " (chord, protocol) pairs, not asserted against itself");
  }

  // ---- 2. MONOTONICITY: an enhanced protocol only ever ADDS -------------------------
  // kitty and modifyOtherKeys are enhancements, not replacements, so anything legacy can
  // deliver they can too. Without this a "fix" that traded one row for another would
  // still pass block 1.
  {
    bool monotone = true;
    std::string broke;
    for (const RolltuiChord& k : universe()) {
      if (deliverable(k, KeyProtocol::Legacy) && !deliverable(k, KeyProtocol::ModifyOtherKeys)) {
        monotone = false;
        broke = show(k);
      }
      if (deliverable(k, KeyProtocol::ModifyOtherKeys) && !deliverable(k, KeyProtocol::Kitty)) {
        monotone = false;
        broke = show(k);
      }
    }
    check(monotone, "Legacy ⊆ ModifyOtherKeys ⊆ Kitty: an enhanced protocol never loses a chord [" + broke + "]");
  }

  // ---- 3. THE ROWS THE MILESTONE IS ABOUT, NAMED ------------------------------------
  {
    const RolltuiChord ctrl_shift_p = ch(U'p', true, false, true);
    check(!deliverable(ctrl_shift_p, KeyProtocol::Legacy) && !deliverable(ctrl_shift_p, KeyProtocol::ModifyOtherKeys) &&
              deliverable(ctrl_shift_p, KeyProtocol::Kitty),
          "ctrl+shift+p: not on legacy, not on modifyOtherKeys, yes on kitty — the milestone's own chord");
    check(encode_key(ctrl_shift_p, KeyProtocol::Legacy) == encode_key(ch(U'p', true), KeyProtocol::Legacy),
          "…and the REASON is in the bytes: legacy sends ctrl+shift+p and ctrl+p the same 0x10 [kitty: ctrl+shift is "
          "not representable]");
    check(encode_key(ctrl_shift_p, KeyProtocol::Kitty) == std::string("\x1b[112;6u"),
          "…while kitty sends CSI 112;6u — the UNSHIFTED code 112, modifiers 1+shift+ctrl [kitty]");
    check(undeliverable_reason(ctrl_shift_p, KeyProtocol::Legacy) == "it needs the kitty keyboard protocol",
          "…and the reason names what to turn on [" + undeliverable_reason(ctrl_shift_p, KeyProtocol::Legacy) + "]");

    // THE ROW A GUESSED BLACKLIST GETS WRONG. "ctrl+shift+anything needs kitty" is the
    // obvious rule and it is false: the functional keys have carried a modifier
    // parameter since xterm, which is why the shipped bindings can and do use this.
    const RolltuiChord ctrl_shift_left = key(ROLLTUI_KEY_LEFT, true, false, true);
    check(deliverable(ctrl_shift_left, KeyProtocol::Legacy) &&
              encode_key(ctrl_shift_left, KeyProtocol::Legacy) == std::string("\x1b[1;6D"),
          "ctrl+shift+left IS deliverable on a plain terminal: CSI 1;6D [xterm ctlseqs; kitty legacy functional table]");
    check(deliverable(key(kF12, true, true, true), KeyProtocol::Legacy),
          "…and so is ctrl+alt+shift+f12: every combination, on every functional key");

    // The other side of the same coin: Enter/Tab/Backspace/Escape are NOT functional
    // keys and carry almost nothing.
    check(!deliverable(key(ROLLTUI_KEY_ENTER, false, false, true), KeyProtocol::Legacy) &&
              !deliverable(key(ROLLTUI_KEY_ENTER, true), KeyProtocol::Legacy) &&
              deliverable(key(ROLLTUI_KEY_ENTER, false, true), KeyProtocol::Legacy),
          "shift+enter and ctrl+enter are legacy-undeliverable (all three are 0x0D); alt+enter is ESC CR and works");
    check(deliverable(key(ROLLTUI_KEY_ENTER, false, false, true), KeyProtocol::Kitty) &&
              encode_key(key(ROLLTUI_KEY_ENTER, false, false, true), KeyProtocol::Kitty) == std::string("\x1b[13;2u"),
          "…and kitty tells Shift+Enter from Enter: CSI 13;2u [kitty functional key codes: ENTER 13]");
    check(deliverable(key(ROLLTUI_KEY_TAB, false, false, true), KeyProtocol::Legacy) &&
              encode_key(key(ROLLTUI_KEY_TAB, false, false, true), KeyProtocol::Legacy) == std::string("\x1b[Z"),
          "shift+tab is the exception on Tab: back-tab, CSI Z — which is why stack.focus_prev may use it");
    check(!deliverable(key(ROLLTUI_KEY_TAB, true), KeyProtocol::Legacy) &&
              !deliverable(key(ROLLTUI_KEY_ESCAPE, false, true), KeyProtocol::Legacy),
          "…but ctrl+tab is 0x09 (Tab's own byte) and alt+escape is ESC ESC (two Escapes): neither survives");

    // The control-code collisions, each named for the key that already owns the byte.
    check(!deliverable(ch(U'h', true), KeyProtocol::Legacy) && !deliverable(ch(U'i', true), KeyProtocol::Legacy) &&
              !deliverable(ch(U'm', true), KeyProtocol::Legacy) && !deliverable(ch(U'[', true), KeyProtocol::Legacy) &&
              !deliverable(ch(U'?', true), KeyProtocol::Legacy) && !deliverable(ch(U'@', true), KeyProtocol::Legacy),
          "ctrl+h/i/m/[/?/@ are Backspace, Tab, Enter, Escape, Backspace and ctrl+space's own bytes: refused");
    check(deliverable(ch(U'j', true), KeyProtocol::Legacy) && deliverable(ch(U' ', true), KeyProtocol::Legacy) &&
              deliverable(ch(U'\\', true), KeyProtocol::Legacy),
          "…while ctrl+j (0x0A is nobody else's in raw mode), ctrl+space and ctrl+backslash are fine");
    check(!deliverable(ch(U'/', true), KeyProtocol::Legacy) && deliverable(ch(U'/', true), KeyProtocol::ModifyOtherKeys) &&
              encode_key(ch(U'/', true), KeyProtocol::ModifyOtherKeys) == std::string("\x1b[27;5;47~"),
          "ctrl+/ has no control code at all — this is what modifyOtherKeys was FOR: \\e[27;5;47~ [xterm's own example]");

    // Shift on a character is not a chord anywhere: the layout folded it in.
    check(!deliverable(ch(U'p', false, false, true), KeyProtocol::Kitty) &&
              undeliverable_reason(ch(U'p', false, false, true), KeyProtocol::Kitty).find("shifted character") !=
                  std::string::npos,
          "shift+p is the character 'P' in every protocol, kitty included — and the reason says so");
    // …with exactly one multi-modifier exception legacy really does carry.
    check(deliverable(ch(U'b', false, true, true), KeyProtocol::Legacy) &&
              encode_key(ch(U'b', false, true, true), KeyProtocol::Legacy) == std::string("\x1b" "B"),
          "alt+shift+b IS legacy-deliverable, as ESC + the uppercase letter [kitty: 'other than shift+alt and ctrl+alt']");
    check(!deliverable(ch(U'b', true, true, true), KeyProtocol::Legacy),
          "…but ctrl+alt+shift+b is not: the ctrl folds the letter to a control code and the shift is gone again");
  }

  // ---- 4. THE LOADER: refused by name, kept, and the SAME FILE clean on kitty --------
  {
    const char* file = R"({"name":"p","bindings":{"input.submit":["enter"],"app.palette":["ctrl+shift+p"],"app.help":["f1"]}})";
    BindingsLoadReport legacy;
    BindingsPtr b = bindings_from_json(file, legacy, KeyProtocol::Legacy);
    check(b && !legacy.clean() && legacy.undeliverable.size() == 1 &&
              legacy.undeliverable[0] ==
                  "app.palette: 'ctrl+shift+p' cannot be delivered by this terminal; it needs the kitty keyboard protocol",
          "the loader names the reason [" + (legacy.undeliverable.empty() ? "" : legacy.undeliverable[0]) + "]");
    check(b && legacy.bad_chords.empty(), "…as its own kind of problem, not as 'not a chord': it parses perfectly");

    // A TERMINAL THAT DOES NEGOTIATE KITTY TAKES THE SAME FILE WITH NO COMPLAINT. This
    // is the assertion that proves the answer is the terminal's and not a blacklist:
    // one file, one loader, two verdicts, decided only by the protocol argument.
    BindingsLoadReport kitty;
    BindingsPtr k = bindings_from_json(file, kitty, KeyProtocol::Kitty);
    check(k && kitty.clean(), "…and the SAME FILE loads clean under kitty [" + kitty.summary() + "]");

    // Kept, not dropped: today's terminal is not tomorrow's, and a bindings file is the
    // user's. The row round-trips through save; what changes is that nothing emits it.
    check(b && chords_for(b.get(), "app.palette").size() == 1,
          "a refused chord is KEPT in the table, so `bindings save` never eats it");
    BindingsLoadReport rt;
    BindingsPtr back = bindings_from_json(bindings_to_json(b.get(), "p"), rt, KeyProtocol::Kitty);
    check(back && rt.clean() && chords_for(back.get(), "app.palette").size() == 1,
          "…and comes back alive on a terminal that can deliver it, with no edit to the file");

    // Inert, and invisible with it.
    set_active_key_protocol(KeyProtocol::Legacy);
    declare(b.get(), {{"app.palette", "the palette"}});
    check(action_for(b.get(), ch(U'p', true, false, true), "app").empty(),
          "…while on this terminal it answers no key: kept and inert, exactly as an undeclared action's row is");
    check(chords_text(b.get(), "app.palette").empty() && !chords_for(b.get(), "app.palette").empty(),
          "…and the HELP form drops it while the table keeps it: a shortcut printed is a promise the key works");
    set_active_key_protocol(KeyProtocol::Kitty);
    check(action_for(b.get(), ch(U'p', true, false, true), "app") == "app.palette" &&
              chords_text(b.get(), "app.palette") == "Ctrl-Shift-P",
          "…and on kitty the very same table answers the key and prints the shortcut");
    set_active_key_protocol(KeyProtocol::Legacy);

    // Neither of the two mercy rungs may become an undeliverability refusal by accident.
    BindingsLoadReport other;
    BindingsPtr o = bindings_from_json(
        R"({"name":"o","bindings":{"input.submit":["enter"],"other.thing":["f9"],"playground.quit":["ctrl+q"]}})",
        other, KeyProtocol::Legacy);
    check(o && other.clean() && other.migrated.size() == 1 && chords_for(o.get(), "studio.quit").size() == 1 &&
              chords_for(o.get(), "other.thing").size() == 1,
          "another screen's action is still kept and a renamed one still migrated: deliverability touches neither");
  }

  // ---- 5. THE SHIPPED FILE IS DELIVERABLE EVERYWHERE ---------------------------------
  // default_bindings() aborts the build if it is not — asserted here so the failure has
  // a name as well as an exit status, the way Phase 11 m1's tool-row abort is.
  {
    BindingsLoadReport rep;
    BindingsPtr d = bindings_from_json(default_bindings_json(), rep, KeyProtocol::Legacy);
    std::string named;
    for (const std::string& u : rep.undeliverable) named += " " + u;
    check(d && rep.undeliverable.empty(),
          "every chord in the shipped bindings file is deliverable under the WEAKEST protocol —" +
              (named.empty() ? std::string(" none undeliverable") : named));
    check(d && rep.clean(), "…and the file is otherwise clean [" + rep.summary() + "]");
  }

  // ---- 6. NEGOTIATION: the answer comes from the terminal ----------------------------
  {
    check(negotiated_with("\x1b[?1u") == KeyProtocol::Kitty,
          "a terminal replying CSI ? 1 u before its DA is negotiated as kitty [kitty: query CSI ? u]");
    check(negotiated_with("\x1b[>4;2m") == KeyProtocol::ModifyOtherKeys,
          "a terminal replying CSI > 4 ; 2 m is negotiated as modifyOtherKeys [xterm XTQUERYMODIFIERS]");
    check(negotiated_with("") == KeyProtocol::Legacy,
          "a terminal that answers only the DA gets LEGACY — the conservative model, and the definitive negative");
    check(negotiated_with("\x1b[?99;7x") == KeyProtocol::Legacy,
          "…and so does one that answers something nobody asked for: every uncertainty degrades DOWN");
  }

  return report("rolltui deliverability_test");
}
