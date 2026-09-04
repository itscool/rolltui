// rolltui/KeysCpp.cpp — the C++ side of the input decoder and the deliverability model,
// behind the same boundary as `c/rolltui_keys.c` (Phase 15 m3). One of the two links; the
// flag `-DROLLTUI_C` picks which. See rolltui_keys.h for the boundary's rules and
// rolltui/Keys.hpp for the decoding and protocol rules themselves.
//
// This is the shape the module has always had — `std::string` for the pending bytes,
// `std::vector<int>` for the CSI parameters, `std::optional<std::string>` for an encoding
// — kept deliberately, so the two implementations differ in the way the languages do and
// not because one of them was rewritten while it was being moved.
#include "rolltui/c/rolltui_keys.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Unicode.hpp"

namespace {

using rolltui::unicode::DecodedChar;

RolltuiChord chord_key(unsigned char k, bool ctrl = false, bool alt = false, bool shift = false) {
  RolltuiChord c;
  c.key = k;
  c.ctrl = ctrl;
  c.alt = alt;
  c.shift = shift;
  return c;
}

RolltuiChord chord_char(char32_t cp, bool ctrl = false, bool alt = false) {
  RolltuiChord c = chord_key(ROLLTUI_KEY_CHAR, ctrl, alt);
  c.ch = cp;
  return c;
}

void emit_key(RolltuiEventFn emit, void* ctx, RolltuiChord k, std::string_view text = {}, bool has_text = false) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key = k;
  e.text = has_text ? text.data() : nullptr;
  e.text_len = has_text ? text.size() : 0;
  emit(ctx, &e);
}

void emit_mouse(RolltuiEventFn emit, void* ctx, const RolltuiMouseEvent& m) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_MOUSE;
  e.mouse = m;
  emit(ctx, &e);
}

void emit_paste(RolltuiEventFn emit, void* ctx, std::string_view text) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_PASTE;
  e.text = text.data();
  e.text_len = text.size();
  emit(ctx, &e);
}

// True when the bytes at `pos` are the start of a UTF-8 sequence that is not yet complete
// but could still become valid with more input.
bool could_complete(std::string_view s, std::size_t pos) {
  unsigned char lead = static_cast<unsigned char>(s[pos]);
  std::size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;
  if (lead < 0xC2 || lead > 0xF4) return false;
  if (s.size() - pos >= need) return false;
  for (std::size_t i = pos + 1; i < s.size(); ++i)
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return false;
  return true;
}

void apply_modifier(RolltuiChord& e, int mod) {  // xterm: mod = 1 + (shift 1 | alt 2 | ctrl 4)
  if (mod < 2) return;
  int bits = mod - 1;
  e.shift = (bits & 1) != 0;
  e.alt = (bits & 2) != 0;
  e.ctrl = (bits & 4) != 0;
}

// One control byte (< 0x20 or 0x7F) as a key. 0x0A is Ctrl-J and not Enter — see Keys.hpp.
RolltuiChord control(unsigned char c) {
  switch (c) {
    case 0x0D: return chord_key(ROLLTUI_KEY_ENTER);
    case 0x09: return chord_key(ROLLTUI_KEY_TAB);
    case 0x7F: case 0x08: return chord_key(ROLLTUI_KEY_BACKSPACE);
    case 0x1B: return chord_key(ROLLTUI_KEY_ESCAPE);
    case 0x00: return chord_char(' ', true);
    case 0x1C: return chord_char('\\', true);
    case 0x1D: return chord_char(']', true);
    case 0x1E: return chord_char('^', true);
    case 0x1F: return chord_char('_', true);
    default: return chord_char(static_cast<char32_t>('a' + c - 1), true);  // 0x01..0x1A
  }
}

// Parses "ESC [ params final"; the length consumed, 0 if incomplete, -1 if malformed.
long csi_length(std::string_view s) {
  std::size_t i = 2;
  while (i < s.size() && s[i] >= 0x30 && s[i] <= 0x3F) ++i;
  while (i < s.size() && s[i] >= 0x20 && s[i] <= 0x2F) ++i;
  if (i >= s.size()) return 0;
  if (s[i] >= 0x40 && s[i] <= 0x7E) return static_cast<long>(i + 1);
  return -1;
}

// Decimal digits only, stopping at anything else — ':' above all, which is what makes
// kitty's sub-parameters read as the base key plus its modifiers (Keys.hpp).
std::vector<int> params(std::string_view p) {
  std::vector<int> v;
  std::size_t start = 0;
  while (start <= p.size()) {
    std::size_t semi = p.find(';', start);
    std::string_view piece = p.substr(start, semi == std::string_view::npos ? std::string_view::npos : semi - start);
    int n = 0;
    for (char c : piece) {
      if (c < '0' || c > '9') break;
      n = n * 10 + (c - '0');
    }
    v.push_back(n);
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  return v;
}

// One key of an ENHANCED report as a chord; nullopt for a code we have no key for.
std::optional<RolltuiChord> enhanced_key(int code, int mod) {
  if (code <= 0) return std::nullopt;
  RolltuiChord e = chord_key(ROLLTUI_KEY_CHAR);
  switch (code) {
    case 13: e.key = ROLLTUI_KEY_ENTER; break;
    case 9: e.key = ROLLTUI_KEY_TAB; break;
    case 127: e.key = ROLLTUI_KEY_BACKSPACE; break;
    case 27: e.key = ROLLTUI_KEY_ESCAPE; break;
    default:
      if (code >= 0xE000 || code > 0x10FFFF) return std::nullopt;
      e.key = ROLLTUI_KEY_CHAR;
      e.ch = static_cast<char32_t>(code);
      break;
  }
  apply_modifier(e, mod);
  return e;
}

bool decode_csi(std::string_view seq, RolltuiEventFn emit, void* ctx) {
  char final = seq.back();
  std::string_view body = seq.substr(2, seq.size() - 3);
  if (!body.empty() && body[0] == '<' && (final == 'M' || final == 'm')) {  // SGR mouse
    std::vector<int> p = params(body.substr(1));
    if (p.size() < 3) return false;
    int b = p[0];
    RolltuiMouseEvent m{};
    m.x = p[1] - 1;
    m.y = p[2] - 1;
    m.shift = (b & 4) != 0;
    m.alt = (b & 8) != 0;
    m.ctrl = (b & 16) != 0;
    int low = b & 3;
    bool motion = b & 32;
    if (b & 64) {
      // xterm buttons 4-7: 64 up, 65 down, 66 left, 67 right. Left/right are a trackpad's
      // sideways ticks and must never become vertical scrolling.
      static const RolltuiMouseEvent::Kind wheel[4] = {
          RolltuiMouseEvent::Kind::WheelUp, RolltuiMouseEvent::Kind::WheelDown,
          RolltuiMouseEvent::Kind::WheelLeft, RolltuiMouseEvent::Kind::WheelRight};
      m.kind = wheel[low];
      m.button = 0;
    } else if (motion) {
      m.kind = (low == 3) ? RolltuiMouseEvent::Kind::Move : RolltuiMouseEvent::Kind::Drag;
      m.button = (low == 3) ? 0 : low + 1;
    } else if (final == 'm' || low == 3) {
      m.kind = RolltuiMouseEvent::Kind::Release;
      m.button = (low == 3) ? 0 : low + 1;
    } else {
      m.kind = RolltuiMouseEvent::Kind::Press;
      m.button = low + 1;
    }
    emit_mouse(emit, ctx, m);
    return true;
  }
  std::vector<int> p = params(body);
  int mod = p.size() >= 2 ? p[1] : 1;
  // The two ENHANCED forms (Phase 12 m3) — see Keys.hpp for both specs.
  if (final == 'u' && !p.empty()) {
    if (std::optional<RolltuiChord> k = enhanced_key(p[0], mod)) { emit_key(emit, ctx, *k); return true; }
    return false;
  }
  if (final == '~' && p.size() >= 3 && p[0] == 27) {
    if (std::optional<RolltuiChord> k = enhanced_key(p[2], p[1])) { emit_key(emit, ctx, *k); return true; }
    return false;
  }
  RolltuiChord e{};
  switch (final) {
    case 'A': e = chord_key(ROLLTUI_KEY_UP); break;
    case 'B': e = chord_key(ROLLTUI_KEY_DOWN); break;
    case 'C': e = chord_key(ROLLTUI_KEY_RIGHT); break;
    case 'D': e = chord_key(ROLLTUI_KEY_LEFT); break;
    case 'H': e = chord_key(ROLLTUI_KEY_HOME); break;
    case 'F': e = chord_key(ROLLTUI_KEY_END); break;
    case 'P': e = chord_key(ROLLTUI_KEY_F1); break;
    case 'Q': e = chord_key(ROLLTUI_KEY_F1 + 1); break;
    case 'R': e = chord_key(ROLLTUI_KEY_F1 + 2); break;
    case 'S': e = chord_key(ROLLTUI_KEY_F1 + 3); break;
    case 'Z': e = chord_key(ROLLTUI_KEY_TAB, false, false, true); break;
    case '~': {
      int n = p.empty() ? 0 : p[0];
      switch (n) {
        case 1: case 7: e = chord_key(ROLLTUI_KEY_HOME); break;
        case 2: e = chord_key(ROLLTUI_KEY_INSERT); break;
        case 3: e = chord_key(ROLLTUI_KEY_DELETE); break;
        case 4: case 8: e = chord_key(ROLLTUI_KEY_END); break;
        case 5: e = chord_key(ROLLTUI_KEY_PAGEUP); break;
        case 6: e = chord_key(ROLLTUI_KEY_PAGEDOWN); break;
        case 11: e = chord_key(ROLLTUI_KEY_F1); break;
        case 12: e = chord_key(ROLLTUI_KEY_F1 + 1); break;
        case 13: e = chord_key(ROLLTUI_KEY_F1 + 2); break;
        case 14: e = chord_key(ROLLTUI_KEY_F1 + 3); break;
        case 15: e = chord_key(ROLLTUI_KEY_F1 + 4); break;
        case 17: e = chord_key(ROLLTUI_KEY_F1 + 5); break;
        case 18: e = chord_key(ROLLTUI_KEY_F1 + 6); break;
        case 19: e = chord_key(ROLLTUI_KEY_F1 + 7); break;
        case 20: e = chord_key(ROLLTUI_KEY_F1 + 8); break;
        case 21: e = chord_key(ROLLTUI_KEY_F1 + 9); break;
        case 23: e = chord_key(ROLLTUI_KEY_F1 + 10); break;
        case 24: e = chord_key(ROLLTUI_KEY_F12); break;
        default: return false;
      }
      break;
    }
    default: return false;
  }
  apply_modifier(e, mod);
  emit_key(emit, ctx, e);
  return true;
}

int mod_bits(const RolltuiChord& k) { return (k.shift ? 1 : 0) | (k.alt ? 2 : 0) | (k.ctrl ? 4 : 0); }

// The keys legacy already parameterises.
bool is_functional(unsigned char k) {
  switch (k) {
    case ROLLTUI_KEY_CHAR: case ROLLTUI_KEY_ENTER: case ROLLTUI_KEY_TAB:
    case ROLLTUI_KEY_BACKSPACE: case ROLLTUI_KEY_ESCAPE: case ROLLTUI_KEY_UNKNOWN:
      return false;
    default:
      return true;
  }
}

// nullopt for a key that is not functional.
std::optional<std::string> legacy_functional(const RolltuiChord& k, int bits) {
  struct Letter { unsigned char key; char final; };
  static constexpr Letter kLetters[] = {
      {ROLLTUI_KEY_UP, 'A'},   {ROLLTUI_KEY_DOWN, 'B'},   {ROLLTUI_KEY_RIGHT, 'C'}, {ROLLTUI_KEY_LEFT, 'D'},
      {ROLLTUI_KEY_HOME, 'H'}, {ROLLTUI_KEY_END, 'F'},    {ROLLTUI_KEY_F1, 'P'},    {ROLLTUI_KEY_F1 + 1, 'Q'},
      {ROLLTUI_KEY_F1 + 2, 'R'}, {ROLLTUI_KEY_F1 + 3, 'S'}};
  struct Tilde { unsigned char key; int n; };
  static constexpr Tilde kTildes[] = {
      {ROLLTUI_KEY_INSERT, 2},   {ROLLTUI_KEY_DELETE, 3},   {ROLLTUI_KEY_PAGEUP, 5}, {ROLLTUI_KEY_PAGEDOWN, 6},
      {ROLLTUI_KEY_F1 + 4, 15},  {ROLLTUI_KEY_F1 + 5, 17},  {ROLLTUI_KEY_F1 + 6, 18}, {ROLLTUI_KEY_F1 + 7, 19},
      {ROLLTUI_KEY_F1 + 8, 20},  {ROLLTUI_KEY_F1 + 9, 21},  {ROLLTUI_KEY_F1 + 10, 23}, {ROLLTUI_KEY_F12, 24}};
  const std::string mod = std::to_string(1 + bits);
  for (const Letter& l : kLetters)
    if (l.key == k.key) return bits == 0 ? std::string("\x1b[") + l.final : "\x1b[1;" + mod + l.final;
  for (const Tilde& t : kTildes)
    if (t.key == k.key)
      return bits == 0 ? "\x1b[" + std::to_string(t.n) + "~" : "\x1b[" + std::to_string(t.n) + ";" + mod + "~";
  return std::nullopt;
}

// The ASCII control code a terminal sends for ctrl+<this character>, or nullopt.
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

// The control codes that ALREADY belong to another key — the six, and why ctrl+j is not
// among them, are in Keys.hpp.
bool control_code_is_its_own(char32_t ch) {
  const char32_t lower = (ch >= U'A' && ch <= U'Z') ? ch - U'A' + U'a' : ch;
  switch (lower) {
    case U'h': case U'i': case U'm': case U'[': case U'?': case U'@': return false;
    default: return ascii_control(ch).has_value();
  }
}

// The unshifted code an enhanced report carries for a key.
int base_code(const RolltuiChord& k) {
  switch (k.key) {
    case ROLLTUI_KEY_ENTER: return 13;
    case ROLLTUI_KEY_TAB: return 9;
    case ROLLTUI_KEY_BACKSPACE: return 127;
    case ROLLTUI_KEY_ESCAPE: return 27;
    default: return static_cast<int>(k.ch);
  }
}

bool is_letter(char32_t c) { return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z'); }

char32_t transmitted_char(const RolltuiChord& k) {
  return (k.shift && k.ch >= U'a' && k.ch <= U'z') ? k.ch - U'a' + U'A' : k.ch;
}

bool starts_a_sequence(char32_t c) { return c == U'[' || c == U'O'; }

// Deliverable under LEGACY, for the keys legacy did not parameterise — the floor all three
// protocols stand on.
bool legacy_ok(const RolltuiChord& k) {
  const int bits = mod_bits(k);
  if (bits == 0) return true;
  if (k.key == ROLLTUI_KEY_CHAR) {
    if (!k.ctrl && !k.alt) return false;  // shift alone is the shifted character
    if (k.ctrl) {
      if (k.shift) return false;  // ctrl folds the letter to a control code; the shift is gone
      return control_code_is_its_own(k.ch);
    }
    if (k.shift && !is_letter(k.ch)) return false;
    return !starts_a_sequence(transmitted_char(k));
  }
  if (k.shift) return k.key == ROLLTUI_KEY_TAB && bits == 1;  // back-tab (CBT), and nothing else
  switch (k.key) {
    case ROLLTUI_KEY_ENTER: case ROLLTUI_KEY_BACKSPACE: return bits == 2;  // alt only, as an ESC prefix
    default: return false;                                                 // Tab and Escape carry nothing else
  }
}

// Legacy, for the keys legacy did NOT parameterise; also what the enhanced protocols send
// for an unmodified key.
std::optional<std::string> legacy_other(const RolltuiChord& k, int bits) {
  switch (k.key) {
    case ROLLTUI_KEY_TAB:
      if (bits == 0) return std::string("\t");
      if (bits == 1) return std::string("\x1b[Z");  // back-tab (CBT) — shift, and only shift
      return std::nullopt;
    case ROLLTUI_KEY_ENTER:
      if (bits == 0) return std::string("\r");
      if (bits == 2) return std::string("\x1b\r");
      return std::nullopt;
    case ROLLTUI_KEY_BACKSPACE:
      if (bits == 0) return std::string("\x7f");
      if (bits == 2) return std::string("\x1b\x7f");
      return std::nullopt;
    case ROLLTUI_KEY_ESCAPE:
      return bits == 0 ? std::optional<std::string>("\x1b") : std::nullopt;
    case ROLLTUI_KEY_CHAR: {
      // The bytes as they really are, AMBIGUOUS ONES INCLUDED — see Keys.hpp.
      if (k.shift && !is_letter(k.ch) && !k.ctrl) return std::nullopt;
      std::string s;
      if (k.alt) s += '\x1b';
      if (k.ctrl) {
        const std::optional<char> c0 = ascii_control(k.ch);
        if (!c0) return std::nullopt;
        s += *c0;
      } else {
        rolltui::unicode::append_utf8(s, transmitted_char(k));
      }
      return s;
    }
    default:
      return std::nullopt;
  }
}

std::atomic<unsigned char> g_protocol{ROLLTUI_PROTOCOL_LEGACY};

}  // namespace

// ---- the decoder ---------------------------------------------------------------------
// OWNED, LONG-LIVED: two `std::string`s, which is what this side of the boundary has for
// a buffer that grows and is consumed from the front.
struct RolltuiKeyDecoder {
  std::string buf;
  std::string paste;
  bool in_paste = false;
};

RolltuiKeyDecoder* rolltui_key_decoder_new(void) { return std::make_unique<RolltuiKeyDecoder>().release(); }
void rolltui_key_decoder_free(RolltuiKeyDecoder* d) {
  const std::unique_ptr<RolltuiKeyDecoder> owned(d);  // takes it back, and frees it on the way out
}
int rolltui_key_decoder_pending(const RolltuiKeyDecoder* d) { return d && !d->buf.empty(); }
int rolltui_key_decoder_in_paste(const RolltuiKeyDecoder* d) { return d && d->in_paste; }

void rolltui_key_decoder_feed(RolltuiKeyDecoder* d, const char* bytes, std::size_t len, RolltuiEventFn emit,
                              void* ctx) {
  if (bytes && len) d->buf.append(bytes, len);
  for (;;) {
    if (d->buf.empty()) break;
    if (d->in_paste) {
      std::size_t end = d->buf.find("\x1b[201~");
      if (end == std::string::npos) {
        // Keep everything that cannot be the start of the terminator.
        std::size_t keep = 0;
        for (std::size_t k = 1; k < 6 && k <= d->buf.size(); ++k)
          if (d->buf.compare(d->buf.size() - k, k, "\x1b[201~", k) == 0) keep = k;
        d->paste.append(d->buf, 0, d->buf.size() - keep);
        d->buf.erase(0, d->buf.size() - keep);
        break;
      }
      d->paste.append(d->buf, 0, end);
      d->buf.erase(0, end + 6);
      emit_paste(emit, ctx, d->paste);
      d->paste.clear();
      d->in_paste = false;
      continue;
    }
    unsigned char c0 = static_cast<unsigned char>(d->buf[0]);
    if (c0 != 0x1B) {
      if (c0 < 0x20 || c0 == 0x7F) {
        emit_key(emit, ctx, control(c0));
        d->buf.erase(0, 1);
        continue;
      }
      DecodedChar dc = rolltui::unicode::decode_one(d->buf, 0);
      // A truncated multi-byte sequence at the end waits for more — but only if more could
      // ever complete it; a stray 0xFF is U+FFFD right now.
      if (!dc.valid && could_complete(d->buf, 0)) break;
      emit_key(emit, ctx, chord_char(dc.cp));
      d->buf.erase(0, dc.length);
      continue;
    }
    // ESC ...
    if (d->buf.size() == 1) break;  // lone ESC: wait for more or flush()
    char c1 = d->buf[1];
    if (c1 == '[') {
      long len2 = csi_length(d->buf);
      if (len2 == 0) break;  // incomplete
      if (len2 < 0) {        // malformed: report ESC as Escape and move on
        emit_key(emit, ctx, chord_key(ROLLTUI_KEY_ESCAPE));
        d->buf.erase(0, 1);
        continue;
      }
      const std::size_t seq_len = static_cast<std::size_t>(len2);
      const std::string_view seq(d->buf.data(), seq_len);
      if (seq == "\x1b[200~") { d->buf.erase(0, seq_len); d->in_paste = true; continue; }
      if (seq == "\x1b[201~") { d->buf.erase(0, seq_len); continue; }  // stray terminator
      if (!decode_csi(seq, emit, ctx)) emit_key(emit, ctx, chord_key(ROLLTUI_KEY_UNKNOWN), seq, true);
      // The Unknown event's bytes were a BORROW of `buf` for exactly that emit call; the
      // window closes here, which is why the erase comes after it.
      d->buf.erase(0, seq_len);
      continue;
    }
    if (c1 == 'O') {
      if (d->buf.size() < 3) break;
      RolltuiChord e{};
      bool known = true;
      switch (d->buf[2]) {
        case 'A': e = chord_key(ROLLTUI_KEY_UP); break;
        case 'B': e = chord_key(ROLLTUI_KEY_DOWN); break;
        case 'C': e = chord_key(ROLLTUI_KEY_RIGHT); break;
        case 'D': e = chord_key(ROLLTUI_KEY_LEFT); break;
        case 'H': e = chord_key(ROLLTUI_KEY_HOME); break;
        case 'F': e = chord_key(ROLLTUI_KEY_END); break;
        case 'P': e = chord_key(ROLLTUI_KEY_F1); break;
        case 'Q': e = chord_key(ROLLTUI_KEY_F1 + 1); break;
        case 'R': e = chord_key(ROLLTUI_KEY_F1 + 2); break;
        case 'S': e = chord_key(ROLLTUI_KEY_F1 + 3); break;
        default: e = chord_key(ROLLTUI_KEY_UNKNOWN); known = false; break;
      }
      emit_key(emit, ctx, e, std::string_view(d->buf.data(), 3), !known);
      d->buf.erase(0, 3);
      continue;
    }
    if (static_cast<unsigned char>(c1) == 0x1B) {  // ESC ESC: an Escape, then decide the rest
      emit_key(emit, ctx, chord_key(ROLLTUI_KEY_ESCAPE));
      d->buf.erase(0, 1);
      continue;
    }
    // Alt + key: ESC followed by one character (a control byte becomes Alt+Ctrl+x)
    unsigned char c = static_cast<unsigned char>(c1);
    if (c < 0x20 || c == 0x7F) {
      RolltuiChord e = control(c);
      e.alt = 1;
      emit_key(emit, ctx, e);
      d->buf.erase(0, 2);
      continue;
    }
    DecodedChar dc = rolltui::unicode::decode_one(d->buf, 1);
    if (!dc.valid && could_complete(d->buf, 1)) break;
    // ESC + an UPPERCASE letter is alt+shift+<letter> — see Keys.hpp for why it folds.
    RolltuiChord alt_key = chord_char(dc.cp, false, true);
    if (dc.cp >= U'A' && dc.cp <= U'Z') {
      alt_key.ch = dc.cp - U'A' + U'a';
      alt_key.shift = 1;
    }
    emit_key(emit, ctx, alt_key);
    d->buf.erase(0, 1 + dc.length);
  }
}

void rolltui_key_decoder_flush(RolltuiKeyDecoder* d, RolltuiEventFn emit, void* ctx) {
  if (d->in_paste) {  // an unterminated paste: deliver what arrived
    d->paste.append(d->buf);
    d->buf.clear();
    emit_paste(emit, ctx, d->paste);
    d->paste.clear();
    d->in_paste = false;
    return;
  }
  while (!d->buf.empty()) {
    if (static_cast<unsigned char>(d->buf[0]) == 0x1B) {
      emit_key(emit, ctx, chord_key(ROLLTUI_KEY_ESCAPE));
      d->buf.erase(0, 1);
      // What is left is re-fed, because it may hold whole events (ESC 'a' ESC 'b') and only
      // a feed knows that. The ESC above is already gone, so every turn consumes a byte.
      rolltui_key_decoder_feed(d, nullptr, 0, emit, ctx);
      continue;
    }
    // A truncated UTF-8 sequence: one replacement character per byte.
    emit_key(emit, ctx, chord_char(0xFFFD));
    d->buf.erase(0, 1);
  }
}

// ---- deliverability --------------------------------------------------------------------

unsigned char rolltui_key_active_protocol(void) { return g_protocol.load(std::memory_order_relaxed); }
void rolltui_key_set_active_protocol(unsigned char p) { g_protocol.store(p, std::memory_order_relaxed); }

long rolltui_key_encode(const RolltuiChord* k, unsigned char p, char* out, std::size_t cap) {
  if (cap < ROLLTUI_KEY_ENCODE_MAX) return -1;
  if (k->key == ROLLTUI_KEY_UNKNOWN) return -1;
  const int bits = mod_bits(*k);
  auto hand_over = [&](const std::optional<std::string>& s) -> long {
    if (!s) return -1;
    std::memcpy(out, s->data(), s->size());
    return static_cast<long>(s->size());
  };
  // Functional keys are identical in all three protocols.
  if (std::optional<std::string> f = legacy_functional(*k, bits)) return hand_over(f);

  const std::string mod = std::to_string(1 + bits);
  if (p == ROLLTUI_PROTOCOL_KITTY) {
    // The disambiguate flag — see Keys.hpp for which keys go through CSI u and which keep
    // their legacy bytes.
    if (k->key == ROLLTUI_KEY_ESCAPE) return hand_over("\x1b[27;" + mod + "u");
    if (bits == 0) return hand_over(legacy_other(*k, bits));
    if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt) return -1;  // 'P' is text, not a chord
    return hand_over("\x1b[" + std::to_string(base_code(*k)) + ";" + mod + "u");
  }
  // Legacy first in both remaining protocols — see Keys.hpp for xterm's mode 1.
  if (legacy_ok(*k)) return hand_over(legacy_other(*k, bits));
  if (p == ROLLTUI_PROTOCOL_LEGACY) return hand_over(legacy_other(*k, bits));
  if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt) return -1;
  if (k->shift) return -1;  // unstated by xterm's own docs — see Keys.hpp
  return hand_over("\x1b[27;" + mod + ";" + std::to_string(base_code(*k)) + "~");
}

int rolltui_key_deliverable(const RolltuiChord* k, unsigned char p) {
  if (k->key == ROLLTUI_KEY_UNKNOWN) return 0;
  if (is_functional(k->key)) return 1;  // legacy already parameterises these
  if (legacy_ok(*k)) return 1;          // the floor all three protocols stand on
  if (k->key == ROLLTUI_KEY_CHAR && !k->ctrl && !k->alt) return 0;
  switch (p) {
    case ROLLTUI_PROTOCOL_KITTY: return 1;
    case ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS: return !k->shift;
    default: return 0;
  }
}

int rolltui_key_undeliverable_reason(const RolltuiChord* k, unsigned char p) {
  if (rolltui_key_deliverable(k, p)) return ROLLTUI_UNDELIVERABLE_NONE;
  if (k->key == ROLLTUI_KEY_UNKNOWN) return ROLLTUI_UNDELIVERABLE_NOT_A_KEY;
  if (k->key == ROLLTUI_KEY_CHAR && k->shift && !k->ctrl && !k->alt) return ROLLTUI_UNDELIVERABLE_SHIFT_ON_CHAR;
  if (p == ROLLTUI_PROTOCOL_LEGACY && rolltui_key_deliverable(k, ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS))
    return ROLLTUI_UNDELIVERABLE_NEEDS_ENHANCED;
  if (rolltui_key_deliverable(k, ROLLTUI_PROTOCOL_KITTY)) return ROLLTUI_UNDELIVERABLE_NEEDS_KITTY;
  return ROLLTUI_UNDELIVERABLE_NEVER;
}
