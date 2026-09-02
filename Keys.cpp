// rolltui/Keys.cpp — see Keys.hpp.
#include "rolltui/Keys.hpp"

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
KeyEvent control(unsigned char c) {
  switch (c) {
    case 0x0D: case 0x0A: return key(Key::Enter);
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
      m.kind = (low == 0) ? MouseEvent::Kind::WheelUp : MouseEvent::Kind::WheelDown;
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
    out.emplace_back(chr(d.cp, false, true));
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
      static const char* kinds[] = {"Press", "Release", "Drag", "Move", "WheelUp", "WheelDown"};
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

}  // namespace rolltui
