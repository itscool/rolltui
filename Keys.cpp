// rolltui/Keys.cpp — THE C++ SHAPE of the input decoder and the deliverability model. See
// Keys.hpp for the event model and the rules; the decoder itself is behind
// `rolltui/c/rolltui_keys.h`, in `KeysCpp.cpp` or `c/rolltui_keys.c`, one CMake flag apart
// (plan/phase-15.md m3).
//
// Three things live here and nowhere else, and each is a VOCABULARY the boundary
// deliberately does not carry:
//   - the PROTOCOL names ("legacy" / "modifyOtherKeys" / "kitty"), which a config file and
//     a `--keys` flag spell;
//   - the KEY names `to_string` prints, and the four English sentences an undeliverable
//     chord is refused with — the C classifies and returns a code, the same split m2 made
//     for a refused effect registration;
//   - the conversion from a decoded CHORD to a `KeyEvent`, which is where an Unknown key's
//     bytes are attached. That is the one place `raw` exists at all, so nothing downstream
//     has to remember it is not part of a chord.
#include "rolltui/Keys.hpp"

#include <array>
#include <cctype>

#include "rolltui/Unicode.hpp"

namespace rolltui {

KeyEvent key_event_of(const RolltuiChord& c) {
  KeyEvent e;
  e.key = static_cast<Key>(c.key);
  e.ch = c.ch;
  e.ctrl = c.ctrl != 0;
  e.alt = c.alt != 0;
  e.shift = c.shift != 0;
  return e;
}

// …and the other way. `raw` is DROPPED here rather than cleared at each comparison site:
// the chord is what the boundary takes, so "a KeyEvent's raw bytes are never part of a
// chord" is structural instead of remembered (rolltui/c/rolltui_keys.h).
RolltuiChord chord_of(const KeyEvent& k) {
  RolltuiChord c;
  c.key = static_cast<unsigned char>(k.key);
  c.ch = k.ch;
  c.ctrl = k.ctrl;
  c.alt = k.alt;
  c.shift = k.shift;
  return c;
}

// …and the whole Event, which four files in this library had each written for themselves
// before Phase 17 m1c gave it one home (Keys.hpp says why). The paste text is BORROWED from
// `e` for exactly as long as `e` lives, which is what every C `handle` needs and no longer.
RolltuiEvent c_event_of(const Event& e) {
  RolltuiEvent ev{};
  if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_KEY;
    ev.key = chord_of(*k);
  } else if (const MouseEvent* m = std::get_if<MouseEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_MOUSE;
    ev.mouse = *m;
  } else if (const PasteEvent* p = std::get_if<PasteEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_PASTE;
    ev.text = p->text.data();
    ev.text_len = p->text.size();
  }
  return ev;
}

namespace {

// The sink the decoder emits through: one event appended per call, with the borrowed
// bytes copied HERE, inside the window the boundary states, and nowhere else.
void collect(void* ctx, const RolltuiEvent* e) {
  std::vector<Event>& out = *static_cast<std::vector<Event>*>(ctx);
  switch (e->kind) {
    case ROLLTUI_EVENT_MOUSE:
      out.emplace_back(e->mouse);
      return;
    case ROLLTUI_EVENT_PASTE:
      out.emplace_back(PasteEvent{std::string(e->text, e->text_len)});
      return;
    default: {
      KeyEvent k = key_event_of(e->key);
      if (e->text) k.raw.assign(e->text, e->text_len);
      out.emplace_back(std::move(k));
      return;
    }
  }
}

}  // namespace

std::vector<Event> KeyDecoder::feed(std::string_view bytes) {
  std::vector<Event> out;
  rolltui_key_decoder_feed(d_.get(), bytes.data(), bytes.size(), collect, &out);
  return out;
}

std::vector<Event> KeyDecoder::flush() {
  std::vector<Event> out;
  rolltui_key_decoder_flush(d_.get(), collect, &out);
  return out;
}

std::string to_string(const Event& e) {
  struct V {
    std::string operator()(const KeyEvent& k) const {
      static const char* names[] = {"Char", "Enter", "Tab", "Backspace", "Escape", "Up", "Down", "Left", "Right",
                                    "Home", "End", "PageUp", "PageDown", "Insert", "Delete", "F1", "F2", "F3",
                                    "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "Unknown"};
      std::string s;
      if (k.ctrl) s += "Ctrl+";
      if (k.alt) s += "Alt+";
      if (k.shift) s += "Shift+";
      if (k.key == Key::Char) unicode::append_utf8(s, k.ch);
      else s += names[static_cast<int>(k.key)];
      if (k.key == Key::Unknown) s += "(" + k.raw + ")";
      return s;
    }
    std::string operator()(const MouseEvent& m) const {
      static const char* kinds[] = {"Press", "Release", "Drag", "Move", "WheelUp", "WheelDown", "WheelLeft", "WheelRight"};
      std::string s = "Mouse ";
      if (m.ctrl) s += "Ctrl+";
      if (m.alt) s += "Alt+";
      if (m.shift) s += "Shift+";
      s += kinds[static_cast<int>(m.kind)];
      if (m.button) s += " " + std::to_string(m.button);
      return s + " @" + std::to_string(m.x) + "," + std::to_string(m.y);
    }
    std::string operator()(const PasteEvent& p) const { return "Paste(" + std::to_string(p.text.size()) + " bytes)"; }
    std::string operator()(const ResizeEvent& r) const { return "Resize " + std::to_string(r.w) + "x" + std::to_string(r.h); }
  };
  return std::visit(V{}, e);
}

// ---- deliverability (Phase 12 m3) -------------------------------------------------
//
// The model and its sources are in Keys.hpp; the ENCODINGS and the RULE are both in the C,
// where deliverability_test's enumerated round trip holds them to each other. What is left
// here is the naming.

std::string_view protocol_name(KeyProtocol p) {
  switch (p) {
    case KeyProtocol::Kitty: return "kitty";
    case KeyProtocol::ModifyOtherKeys: return "modifyOtherKeys";
    case KeyProtocol::Legacy: break;
  }
  return "legacy";
}

std::optional<KeyProtocol> parse_key_protocol(std::string_view name) {
  std::string n;
  for (char c : name) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (n == "legacy") return KeyProtocol::Legacy;
  if (n == "modifyotherkeys") return KeyProtocol::ModifyOtherKeys;
  if (n == "kitty") return KeyProtocol::Kitty;
  return std::nullopt;
}

KeyProtocol active_key_protocol() { return static_cast<KeyProtocol>(rolltui_key_active_protocol()); }
void set_active_key_protocol(KeyProtocol p) { rolltui_key_set_active_protocol(static_cast<unsigned char>(p)); }

std::optional<std::string> encode_key(const KeyEvent& k, KeyProtocol p) {
  // CALLER-FILLED, with the bound known WITHOUT asking (rolltui/c/rolltui_keys.h): the
  // longest encoding any protocol produces is stated as a constant, so there is no
  // measure-then-fill round trip.
  char buf[ROLLTUI_KEY_ENCODE_MAX];
  const RolltuiChord c = chord_of(k);
  const long n = rolltui_key_encode(&c, static_cast<unsigned char>(p), buf, sizeof buf);
  if (n < 0) return std::nullopt;
  return std::string(buf, static_cast<std::size_t>(n));
}

bool deliverable(const KeyEvent& k, KeyProtocol p) {
  const RolltuiChord c = chord_of(k);
  return rolltui_key_deliverable(&c, static_cast<unsigned char>(p)) != 0;
}

std::string undeliverable_reason(const KeyEvent& k, KeyProtocol p) {
  // THE WORDS, against the C's classification. The reason names the CHEAPEST protocol that
  // would carry the chord, so a person is told what to turn on rather than that something
  // is impossible.
  static constexpr std::array<std::string_view, 6> kReasons = {
      "",
      "it is not a key",
      "shift on a character key is the shifted character itself, which no terminal reports as a chord",
      "it needs the kitty keyboard protocol or xterm's modifyOtherKeys",
      "it needs the kitty keyboard protocol",
      "no keyboard protocol this library speaks can report it",
  };
  const RolltuiChord c = chord_of(k);
  const int code = rolltui_key_undeliverable_reason(&c, static_cast<unsigned char>(p));
  return std::string(kReasons[static_cast<std::size_t>(code) < kReasons.size() ? code : 0]);
}

}  // namespace rolltui
