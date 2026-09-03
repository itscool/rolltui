// rolltui/Keys.cpp — see Keys.hpp.
#include "rolltui/Keys.hpp"

#include <atomic>
#include <cctype>
#include <cstdlib>

#include "rolltui/Unicode.hpp"

namespace rolltui {

namespace {

KeyEvent key(Key k, bool ctrl = false, bool alt = false, bool shift = false) {
  KeyEvent e;
  e.key = k;
  e.ctrl = ctrl;
  e.alt = alt;
  e.shift = shift;
  return e;
}

// True when the bytes at `pos` are the start of a UTF-8 sequence that is not yet
// complete but could still become valid with more input: a real lead byte (C2-F4)
// followed only by continuation bytes so far. Anything else is decided now.
bool could_complete(std::string_view s, std::size_t pos) {
  unsigned char lead = static_cast<unsigned char>(s[pos]);
  std::size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;
  if (lead < 0xC2 || lead > 0xF4) return false;
  if (s.size() - pos >= need) return false;
  for (std::size_t i = pos + 1; i < s.size(); ++i)
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return false;
  return true;
}

KeyEvent chr(char32_t c, bool ctrl = false, bool alt = false) {
  KeyEvent e;
  e.key = Key::Char;
  e.ch = c;
  e.ctrl = ctrl;
  e.alt = alt;
  return e;
}

void apply_modifier(KeyEvent& e, int mod) {  // xterm: mod = 1 + (shift 1 | alt 2 | ctrl 4)
  if (mod < 2) return;
  int bits = mod - 1;
  e.shift = bits & 1;
  e.alt = bits & 2;
  e.ctrl = bits & 4;
}

// One control byte (< 0x20 or 0x7F) as a key.
//
// 0x0A is Ctrl-J, not Enter (Phase 12 m3). Terminal always runs raw — cfmakeraw clears
// ICRNL and INLCR — so Enter is CR and LF is only ever ctrl+j. Folding the two together
// made `ctrl+j` undeliverable while looking bound; see the note in Keys.hpp. 0x08 stays
// Backspace, because there a real ambiguity exists: `stty erase` and xterm's
// backarrowKey both decide whether Backspace sends 0x08 or 0x7F, so ctrl+h genuinely
// cannot be told apart and is refused instead of guessed.
KeyEvent control(unsigned char c) {
  switch (c) {
    case 0x0D: return key(Key::Enter);
    case 0x09: return key(Key::Tab);
    case 0x7F: case 0x08: return key(Key::Backspace);
    case 0x1B: return key(Key::Escape);
    case 0x00: return chr(' ', true);
    case 0x1C: return chr('\\', true);
    case 0x1D: return chr(']', true);
    case 0x1E: return chr('^', true);
    case 0x1F: return chr('_', true);
    default: return chr(static_cast<char32_t>('a' + c - 1), true);  // 0x01..0x1A
  }
}

// Parses "ESC [ params final"; returns the length consumed, 0 if incomplete, -1 if
// malformed.
long csi_length(std::string_view s) {
  std::size_t i = 2;
  while (i < s.size() && s[i] >= 0x30 && s[i] <= 0x3F) ++i;
  while (i < s.size() && s[i] >= 0x20 && s[i] <= 0x2F) ++i;
  if (i >= s.size()) return 0;
  if (s[i] >= 0x40 && s[i] <= 0x7E) return static_cast<long>(i + 1);
  return -1;
}

std::vector<int> params(std::string_view p) {
  std::vector<int> v;
  std::size_t start = 0;
  while (start <= p.size()) {
    std::size_t semi = p.find(';', start);
    std::string_view piece = p.substr(start, semi == std::string_view::npos ? std::string_view::npos : semi - start);
    v.push_back(piece.empty() ? 0 : std::atoi(std::string(piece).c_str()));
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  return v;
}

// One key of an ENHANCED report (kitty's `CSI code;mod u`, modifyOtherKeys' `CSI
// 27;mod;code ~`) as an event. `code` is the UNSHIFTED codepoint of the key, which is
// why the four named codes below are the legacy control bytes rather than the keys'
// own numbers: kitty's functional-key table gives ENTER 13, TAB 9, BACKSPACE 127,
// ESCAPE 27, and xterm's examples use the same (`\e[27;5;9~` is control-TAB).
// nullopt for a code we have no key for — kitty's private-use range (0xE000+: keypad,
// media and the modifier keys themselves) — so the caller reports the raw bytes as
// Unknown rather than inserting a private-use character into someone's input.
std::optional<KeyEvent> enhanced_key(int code, int mod) {
  if (code <= 0) return std::nullopt;
  KeyEvent e;
  switch (code) {
    case 13: e.key = Key::Enter; break;
    case 9: e.key = Key::Tab; break;
    case 127: e.key = Key::Backspace; break;
    case 27: e.key = Key::Escape; break;
    default:
      if (code >= 0xE000 || code > 0x10FFFF) return std::nullopt;
      e.key = Key::Char;
      e.ch = static_cast<char32_t>(code);
      break;
  }
  apply_modifier(e, mod);
  return e;
}

bool decode_csi(std::string_view seq, std::vector<Event>& out) {
  // seq = ESC [ ... final
  char final = seq.back();
  std::string_view body = seq.substr(2, seq.size() - 3);
  if (!body.empty() && body[0] == '<' && (final == 'M' || final == 'm')) {  // SGR mouse
    std::vector<int> p = params(body.substr(1));
    if (p.size() < 3) return false;
    int b = p[0];
    MouseEvent m;
    m.x = p[1] - 1;
    m.y = p[2] - 1;
    m.shift = b & 4;
    m.alt = b & 8;
    m.ctrl = b & 16;
    int low = b & 3;
    bool motion = b & 32;
    if (b & 64) {
      // xterm buttons 4-7: 64 up, 65 down, 66 left, 67 right. Left/right are a
      // trackpad's sideways ticks and must never become vertical scrolling.
      static const MouseEvent::Kind wheel[4] = {MouseEvent::Kind::WheelUp, MouseEvent::Kind::WheelDown,
                                                MouseEvent::Kind::WheelLeft, MouseEvent::Kind::WheelRight};
      m.kind = wheel[low];
      m.button = 0;
    } else if (motion) {
      m.kind = (low == 3) ? MouseEvent::Kind::Move : MouseEvent::Kind::Drag;
      m.button = (low == 3) ? 0 : low + 1;
    } else if (final == 'm' || low == 3) {
      m.kind = MouseEvent::Kind::Release;
      m.button = (low == 3) ? 0 : low + 1;
    } else {
      m.kind = MouseEvent::Kind::Press;
      m.button = low + 1;
    }
    out.emplace_back(m);
    return true;
  }
  std::vector<int> p = params(body);
  int mod = p.size() >= 2 ? p[1] : 1;
  // The two ENHANCED forms (Phase 12 m3). Both say the same thing in different words: a
  // key code plus a modifier parameter, for the keys the legacy encoding had no room
  // for. Neither is ever sent unless Terminal::negotiate_keyboard() asked for it.
  //   kitty            CSI code ; mod u     [sw.kovidgoyal.net/kitty/keyboard-protocol]
  //   modifyOtherKeys  CSI 27 ; mod ; code ~ [invisible-island.net/xterm/modified-keys]
  // `params()` splits on ';' and atoi() stops at ':', which is exactly right for kitty's
  // sub-parameters: `CSI 97:65;6:1u` reads as code 97, mod 6 — the base key and the
  // modifiers, with the shifted-key alternate and the event type ignored. We never turn
  // on the flags that produce release or repeat events, so every one of them is a press.
  if (final == 'u' && !p.empty()) {
    if (std::optional<KeyEvent> k = enhanced_key(p[0], mod)) { out.emplace_back(*k); return true; }
    return false;
  }
  if (final == '~' && p.size() >= 3 && p[0] == 27) {
    if (std::optional<KeyEvent> k = enhanced_key(p[2], p[1])) { out.emplace_back(*k); return true; }
    return false;
  }
  KeyEvent e;
  switch (final) {
    case 'A': e = key(Key::Up); break;
    case 'B': e = key(Key::Down); break;
    case 'C': e = key(Key::Right); break;
    case 'D': e = key(Key::Left); break;
    case 'H': e = key(Key::Home); break;
    case 'F': e = key(Key::End); break;
    case 'P': e = key(Key::F1); break;
    case 'Q': e = key(Key::F2); break;
    case 'R': e = key(Key::F3); break;
    case 'S': e = key(Key::F4); break;
    case 'Z': e = key(Key::Tab, false, false, true); break;
    case '~': {
      int n = p.empty() ? 0 : p[0];
      switch (n) {
        case 1: case 7: e = key(Key::Home); break;
        case 2: e = key(Key::Insert); break;
        case 3: e = key(Key::Delete); break;
        case 4: case 8: e = key(Key::End); break;
        case 5: e = key(Key::PageUp); break;
        case 6: e = key(Key::PageDown); break;
        case 11: e = key(Key::F1); break;
        case 12: e = key(Key::F2); break;
        case 13: e = key(Key::F3); break;
        case 14: e = key(Key::F4); break;
        case 15: e = key(Key::F5); break;
        case 17: e = key(Key::F6); break;
        case 18: e = key(Key::F7); break;
        case 19: e = key(Key::F8); break;
        case 20: e = key(Key::F9); break;
        case 21: e = key(Key::F10); break;
        case 23: e = key(Key::F11); break;
        case 24: e = key(Key::F12); break;
        default: return false;
      }
      break;
    }
    default: return false;
  }
  apply_modifier(e, mod);
  out.emplace_back(e);
  return true;
}

}  // namespace

std::vector<Event> KeyDecoder::feed(std::string_view bytes) {
  buf_.append(bytes);
  std::vector<Event> out;
  for (;;) {
    if (buf_.empty()) break;
    if (in_paste_) {
      std::size_t end = buf_.find("\x1b[201~");
      if (end == std::string::npos) {
        // Keep everything that cannot be the start of the terminator.
        std::size_t keep = 0;
        for (std::size_t k = 1; k < 6 && k <= buf_.size(); ++k)
          if (buf_.compare(buf_.size() - k, k, "\x1b[201~", k) == 0) keep = k;
        paste_.append(buf_, 0, buf_.size() - keep);
        buf_.erase(0, buf_.size() - keep);
        break;
      }
      paste_.append(buf_, 0, end);
      buf_.erase(0, end + 6);
      out.emplace_back(PasteEvent{std::move(paste_)});
      paste_.clear();
      in_paste_ = false;
      continue;
    }
    unsigned char c0 = static_cast<unsigned char>(buf_[0]);
    if (c0 != 0x1B) {
      if (c0 < 0x20 || c0 == 0x7F) {
        out.emplace_back(control(c0));
        buf_.erase(0, 1);
        continue;
      }
      unicode::DecodedChar d = unicode::decode_one(buf_, 0);
      // A truncated multi-byte sequence at the end of the buffer waits for more —
      // but only if more could ever complete it; a stray 0xFF is U+FFFD right now.
      if (!d.valid && could_complete(buf_, 0)) break;
      out.emplace_back(chr(d.cp));
      buf_.erase(0, d.length);
      continue;
    }
    // ESC ...
    if (buf_.size() == 1) break;  // lone ESC: wait for more or flush()
    char c1 = buf_[1];
    if (c1 == '[') {
      long len = csi_length(buf_);
      if (len == 0) break;  // incomplete
      if (len < 0) {  // malformed: report ESC as Escape and move on
        out.emplace_back(key(Key::Escape));
        buf_.erase(0, 1);
        continue;
      }
      std::string seq = buf_.substr(0, static_cast<std::size_t>(len));
      buf_.erase(0, static_cast<std::size_t>(len));
      if (seq == "\x1b[200~") { in_paste_ = true; continue; }
      if (seq == "\x1b[201~") continue;  // stray terminator
      if (!decode_csi(seq, out)) {
        KeyEvent u = key(Key::Unknown);
        u.raw = seq;
        out.emplace_back(u);
      }
      continue;
    }
    if (c1 == 'O') {
      if (buf_.size() < 3) break;
      KeyEvent e;
      switch (buf_[2]) {
        case 'A': e = key(Key::Up); break;
        case 'B': e = key(Key::Down); break;
        case 'C': e = key(Key::Right); break;
        case 'D': e = key(Key::Left); break;
        case 'H': e = key(Key::Home); break;
        case 'F': e = key(Key::End); break;
        case 'P': e = key(Key::F1); break;
        case 'Q': e = key(Key::F2); break;
        case 'R': e = key(Key::F3); break;
        case 'S': e = key(Key::F4); break;
        default: e = key(Key::Unknown); e.raw = buf_.substr(0, 3); break;
      }
      out.emplace_back(e);
      buf_.erase(0, 3);
      continue;
    }
    if (static_cast<unsigned char>(c1) == 0x1B) {  // ESC ESC: an Escape, then decide the rest
      out.emplace_back(key(Key::Escape));
      buf_.erase(0, 1);
      continue;
    }
    // Alt + key: ESC followed by one character (a control byte becomes Alt+Ctrl+x)
    unsigned char c = static_cast<unsigned char>(c1);
    if (c < 0x20 || c == 0x7F) {
      KeyEvent e = control(c);
      e.alt = true;
      out.emplace_back(e);
      buf_.erase(0, 2);
      continue;
    }
    unicode::DecodedChar d = unicode::decode_one(buf_, 1);
    if (!d.valid && could_complete(buf_, 1)) break;
    // ESC + an UPPERCASE letter is alt+shift+<letter> — the one multi-modifier chord the
    // legacy encoding really does carry (kitty's spec names shift+alt as one of exactly
    // two). Canonical chords are lowercase with a shift flag, so it is folded that way
    // here; reporting Alt+'B' instead made `alt+shift+b` a binding that parses, saves,
    // renders in the help popup and never fires. Letters only: for punctuation the
    // shifted symbol is the layout's business and has no canonical chord spelling.
    KeyEvent alt_key = chr(d.cp, false, true);
    if (d.cp >= U'A' && d.cp <= U'Z') {
      alt_key.ch = d.cp - U'A' + U'a';
      alt_key.shift = true;
    }
    out.emplace_back(alt_key);
    buf_.erase(0, 1 + d.length);
  }
  return out;
}

std::vector<Event> KeyDecoder::flush() {
  std::vector<Event> out;
  if (in_paste_) {  // an unterminated paste: deliver what arrived
    paste_.append(buf_);
    buf_.clear();
    out.emplace_back(PasteEvent{std::move(paste_)});
    paste_.clear();
    in_paste_ = false;
    return out;
  }
  while (!buf_.empty()) {
    if (static_cast<unsigned char>(buf_[0]) == 0x1B) {
      out.emplace_back(key(Key::Escape));
      buf_.erase(0, 1);
      std::string rest;
      rest.swap(buf_);
      std::vector<Event> more = feed(rest);
      out.insert(out.end(), more.begin(), more.end());
      continue;
    }
    // A truncated UTF-8 sequence: one replacement character per byte.
    out.emplace_back(chr(0xFFFD));
    buf_.erase(0, 1);
  }
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
// The model and its sources are in Keys.hpp. Everything here is one of two things: the
// ENCODINGS (encode_key — what a terminal speaking each protocol sends), or the RULE
// (deliverable — a cheap classification, so a key press does not build strings). The
// two are independent statements of the same fact and would be worth nothing if only
// one of them existed: rolltui/tests/deliverability_test.cpp asserts, over an
// enumerated universe of chords, that the rule agrees with feeding the encoding to the
// real KeyDecoder. A row where they disagree fails by name.

namespace {

int mod_bits(const KeyEvent& k) { return (k.shift ? 1 : 0) | (k.alt ? 2 : 0) | (k.ctrl ? 4 : 0); }

// The keys legacy already parameterises: xterm sends `CSI 1 ; mod {ABCDEFHPQS}` or
// `CSI n ; mod ~` for these, so they carry every modifier combination in every protocol.
bool is_functional(Key k) {
  switch (k) {
    case Key::Char: case Key::Enter: case Key::Tab: case Key::Backspace: case Key::Escape: case Key::Unknown:
      return false;
    default:
      return true;
  }
}

// nullopt for a key that is not functional.
std::optional<std::string> legacy_functional(const KeyEvent& k, int bits) {
  struct Letter { Key key; char final; };
  static constexpr Letter kLetters[] = {{Key::Up, 'A'},   {Key::Down, 'B'}, {Key::Right, 'C'}, {Key::Left, 'D'},
                                        {Key::Home, 'H'}, {Key::End, 'F'},  {Key::F1, 'P'},    {Key::F2, 'Q'},
                                        {Key::F3, 'R'},   {Key::F4, 'S'}};
  struct Tilde { Key key; int n; };
  static constexpr Tilde kTildes[] = {{Key::Insert, 2}, {Key::Delete, 3},  {Key::PageUp, 5}, {Key::PageDown, 6},
                                      {Key::F5, 15},    {Key::F6, 17},     {Key::F7, 18},    {Key::F8, 19},
                                      {Key::F9, 20},    {Key::F10, 21},    {Key::F11, 23},   {Key::F12, 24}};
  const std::string mod = std::to_string(1 + bits);
  for (const Letter& l : kLetters)
    if (l.key == k.key) return bits == 0 ? std::string("\x1b[") + l.final : "\x1b[1;" + mod + l.final;
  for (const Tilde& t : kTildes)
    if (t.key == k.key)
      return bits == 0 ? "\x1b[" + std::to_string(t.n) + "~" : "\x1b[" + std::to_string(t.n) + ";" + mod + "~";
  return std::nullopt;
}

// The ASCII control code a terminal sends for ctrl+<this character>, or nullopt when
// there is none (ctrl+1, ctrl+, and every other punctuation mark outside this set send
// the unmodified character, which is why modifyOtherKeys was invented).
std::optional<char> ascii_control(char32_t ch) {
  if (ch >= U'a' && ch <= U'z') return static_cast<char>(ch - U'a' + 1);
  if (ch >= U'A' && ch <= U'Z') return static_cast<char>(ch - U'A' + 1);
  switch (ch) {
    case U' ': case U'@': return '\x00';
    case U'[': return '\x1b';
    case U'\\': return '\x1c';
    case U']': return '\x1d';
    case U'^': return '\x1e';
    case U'_': return '\x1f';
    case U'?': return '\x7f';
    default: return std::nullopt;
  }
}

// The control codes that ALREADY belong to another key, so ctrl+<that character> cannot
// be told apart from it. Six of them, each named for the key that owns the byte:
//   h → 0x08 Backspace   i → 0x09 Tab   m → 0x0D Enter
//   [ → 0x1B Escape      ? → 0x7F Backspace   @ → 0x00 ctrl+space
// ctrl+j is deliberately NOT here: 0x0A is nobody else's once the decoder stops calling
// it Enter (see Keys.hpp). deliverability_test proves this list is exactly the set of
// collisions the encoder produces, so it cannot drift into a guess.
bool control_code_is_its_own(char32_t ch) {
  const char32_t lower = (ch >= U'A' && ch <= U'Z') ? ch - U'A' + U'a' : ch;
  switch (lower) {
    case U'h': case U'i': case U'm': case U'[': case U'?': case U'@': return false;
    default: return ascii_control(ch).has_value();
  }
}

// The unshifted code an enhanced report carries for a key.
int base_code(const KeyEvent& k) {
  switch (k.key) {
    case Key::Enter: return 13;
    case Key::Tab: return 9;
    case Key::Backspace: return 127;
    case Key::Escape: return 27;
    default: return static_cast<int>(k.ch);
  }
}

bool is_letter(char32_t c) { return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z'); }

// The character a legacy terminal actually transmits for this chord: shift folded in,
// which for a letter is its uppercase form and for anything else is the layout's
// business (and so has no chord spelling at all — see legacy_other).
char32_t transmitted_char(const KeyEvent& k) {
  return (k.shift && k.ch >= U'a' && k.ch <= U'z') ? k.ch - U'a' + U'A' : k.ch;
}

// ESC + '[' is the start of a CSI and ESC + 'O' the start of an SS3, so `alt+[` and
// `alt+shift+o` cannot be told from the beginning of a sequence — the decoder waits for
// the rest of one that never comes. Two characters, and they are here because the
// enumerated table in deliverability_test found them, not because anyone predicted them.
bool starts_a_sequence(char32_t c) { return c == U'[' || c == U'O'; }

// Deliverable under LEGACY, for the keys legacy did not parameterise. Named separately
// because every protocol keeps the legacy encodings — kitty and modifyOtherKeys ADD
// forms, they never take one away — so this is the floor all three stand on, and
// deliverability is monotone: Legacy ⊆ ModifyOtherKeys ⊆ Kitty (asserted in the test).
bool legacy_ok(const KeyEvent& k) {
  const int bits = mod_bits(k);
  if (bits == 0) return true;
  if (k.key == Key::Char) {
    if (!k.ctrl && !k.alt) return false;  // shift alone is the shifted character, and nothing more
    if (k.ctrl) {
      if (k.shift) return false;  // ctrl folds the letter to a control code; the shift is gone
      return control_code_is_its_own(k.ch);
    }
    // alt, without ctrl: ESC + the character. shift+alt on a LETTER is the one
    // multi-modifier combination legacy really carries [kitty: "No way to reliably use
    // multiple modifier keys, other than, shift+alt and ctrl+alt"]; on anything else the
    // shifted symbol is the layout's and has no chord spelling.
    if (k.shift && !is_letter(k.ch)) return false;
    return !starts_a_sequence(transmitted_char(k));
  }
  if (k.shift) return k.key == Key::Tab && bits == 1;  // back-tab (CBT), and nothing else
  switch (k.key) {
    case Key::Enter: case Key::Backspace: return bits == 2;  // alt only, as an ESC prefix
    default: return false;                                   // Tab and Escape carry nothing else
  }
}

// Legacy, for the keys legacy did NOT parameterise. Also what the enhanced protocols
// send for an unmodified key, which is why it is its own function.
std::optional<std::string> legacy_other(const KeyEvent& k, int bits) {
  switch (k.key) {
    case Key::Tab:
      if (bits == 0) return std::string("\t");
      if (bits == 1) return std::string("\x1b[Z");  // back-tab (CBT) — shift, and only shift
      return std::nullopt;
    case Key::Enter:
      if (bits == 0) return std::string("\r");
      if (bits == 2) return std::string("\x1b\r");
      return std::nullopt;
    case Key::Backspace:
      if (bits == 0) return std::string("\x7f");
      if (bits == 2) return std::string("\x1b\x7f");
      return std::nullopt;
    case Key::Escape:
      return bits == 0 ? std::optional<std::string>("\x1b") : std::nullopt;
    case Key::Char: {
      // The bytes as they really are, AMBIGUOUS ONES INCLUDED — encode_key states what
      // the terminal sends, not whether it is any use. ctrl+shift+p comes out as "\x10",
      // which is ctrl+p's, and that identity is the evidence for the refusal rather than
      // a claim about it. nullopt only where no bytes exist to name: a shifted symbol
      // (the layout's business, not the terminal's) and a ctrl with no control code.
      if (k.shift && !is_letter(k.ch) && !k.ctrl) return std::nullopt;
      std::string s;
      if (k.alt) s += '\x1b';
      if (k.ctrl) {
        const std::optional<char> c0 = ascii_control(k.ch);
        if (!c0) return std::nullopt;
        s += *c0;
      } else {
        unicode::append_utf8(s, transmitted_char(k));
      }
      return s;
    }
    default:
      return std::nullopt;
  }
}

std::atomic<KeyProtocol> g_protocol{KeyProtocol::Legacy};

}  // namespace

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

KeyProtocol active_key_protocol() { return g_protocol.load(std::memory_order_relaxed); }
void set_active_key_protocol(KeyProtocol p) { g_protocol.store(p, std::memory_order_relaxed); }

std::optional<std::string> encode_key(const KeyEvent& k, KeyProtocol p) {
  if (k.key == Key::Unknown) return std::nullopt;
  const int bits = mod_bits(k);
  // Functional keys are identical in all three: kitty keeps their legacy CSI form (its
  // own legacy functional-key table), and modifyOtherKeys only ever touched the keys
  // that had no modifier parameter to begin with.
  if (std::optional<std::string> f = legacy_functional(k, bits)) return f;

  const std::string mod = std::to_string(1 + bits);
  if (p == KeyProtocol::Kitty) {
    // The disambiguate flag reports "the Esc, alt+key, ctrl+key, ctrl+alt+key,
    // shift+alt+key keys using CSI u sequences instead of legacy ones". Esc goes there
    // even unmodified — telling a lone Esc from the start of a sequence is the flag's
    // whole point — while Enter, Tab and Backspace keep their legacy bytes UNMODIFIED so
    // a wedged shell can still be reset.
    if (k.key == Key::Escape) return "\x1b[27;" + mod + "u";
    if (bits == 0) return legacy_other(k, bits);
    if (k.key == Key::Char && !k.ctrl && !k.alt) return std::nullopt;  // 'P' is text, not a chord
    return "\x1b[" + std::to_string(base_code(k)) + ";" + mod + "u";
  }
  // Legacy first in both remaining protocols: modifyOtherKeys ADDS a form for the
  // combinations that had none, it does not restate the ones that worked. (That is
  // xterm's mode 1, "encode only keys with modifiers that produce non-standard results",
  // which is what negotiate_keyboard asks for — mode 2 also escapes keys that would
  // produce a printable character, and its shift handling is the part xterm's own docs
  // decline to pin down.)
  if (legacy_ok(k)) return legacy_other(k, bits);
  if (p == KeyProtocol::Legacy) return legacy_other(k, bits);  // may be another key's bytes, or none
  if (k.key == Key::Char && !k.ctrl && !k.alt) return std::nullopt;
  if (k.shift) return std::nullopt;  // unstated by xterm's own docs — see Keys.hpp
  return "\x1b[27;" + mod + ";" + std::to_string(base_code(k)) + "~";
}

bool deliverable(const KeyEvent& k, KeyProtocol p) {
  if (k.key == Key::Unknown) return false;
  if (is_functional(k.key)) return true;  // legacy already parameterises these, in every protocol
  if (legacy_ok(k)) return true;          // the floor all three protocols stand on
  if (k.key == Key::Char && !k.ctrl && !k.alt) return false;  // shift alone is the shifted character
  switch (p) {
    case KeyProtocol::Kitty: return true;
    case KeyProtocol::ModifyOtherKeys: return !k.shift;
    case KeyProtocol::Legacy: break;
  }
  return false;
}

std::string undeliverable_reason(const KeyEvent& k, KeyProtocol p) {
  if (deliverable(k, p)) return "";
  if (k.key == Key::Unknown) return "it is not a key";
  if (k.key == Key::Char && k.shift && !k.ctrl && !k.alt)
    return "shift on a character key is the shifted character itself, which no terminal reports as a chord";
  if (p == KeyProtocol::Legacy && deliverable(k, KeyProtocol::ModifyOtherKeys))
    return "it needs the kitty keyboard protocol or xterm's modifyOtherKeys";
  if (deliverable(k, KeyProtocol::Kitty)) return "it needs the kitty keyboard protocol";
  return "no keyboard protocol this library speaks can report it";
}

}  // namespace rolltui
