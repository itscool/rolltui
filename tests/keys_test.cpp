//
// keys_test.cpp — the bytes → events table for KeyDecoder, incl. split feeds, the
// lone-ESC flush, SGR mouse and bracketed paste.
//
#include <string>
#include <vector>

#include "rolltui/Keys.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

std::string names(const std::vector<Event>& v) {
  std::string s;
  for (const Event& e : v) s += (s.empty() ? "" : " | ") + to_string(e);
  return s;
}

void table(const std::string& bytes, const std::string& want, bool flush = false) {
  KeyDecoder d;
  std::vector<Event> ev = d.feed(bytes);
  if (flush) {
    std::vector<Event> more = d.flush();
    ev.insert(ev.end(), more.begin(), more.end());
  }
  std::string got = names(ev);
  std::string shown;
  for (char c : bytes) shown += (c == '\x1b') ? std::string("ESC") : (static_cast<unsigned char>(c) < 0x20 ? "^" + std::string(1, static_cast<char>(c + '@')) : std::string(1, c));
  check(got == want, shown + "  →  " + got + (got == want ? "" : "   (want " + want + ")"));
}

}  // namespace

int main() {
  table("a", "a");
  table("abc", "a | b | c");
  table("\xE4\xB8\xAD", "\xE4\xB8\xAD");
  table("\x01", "Ctrl+a");
  table("\x1a", "Ctrl+z");
  table(std::string("\x00", 1), "Ctrl+ ");
  table("\x1f", "Ctrl+_");
  table("\r", "Enter");
  table("\n", "Enter");
  table("\t", "Tab");
  table("\x7f", "Backspace");
  table("\x08", "Backspace");
  table("\x1b[A", "Up");
  table("\x1b[B", "Down");
  table("\x1b[C", "Right");
  table("\x1b[D", "Left");
  table("\x1b[H", "Home");
  table("\x1b[F", "End");
  table("\x1bOA", "Up");
  table("\x1bOH", "Home");
  table("\x1bOP", "F1");
  table("\x1b[1;5A", "Ctrl+Up");
  table("\x1b[1;2C", "Shift+Right");
  table("\x1b[1;3D", "Alt+Left");
  table("\x1b[1;7B", "Ctrl+Alt+Down");
  table("\x1b[3~", "Delete");
  table("\x1b[3;5~", "Ctrl+Delete");
  table("\x1b[2~", "Insert");
  table("\x1b[5~", "PageUp");
  table("\x1b[6~", "PageDown");
  table("\x1b[1~", "Home");
  table("\x1b[4~", "End");
  table("\x1b[15~", "F5");
  table("\x1b[24~", "F12");
  table("\x1b[Z", "Shift+Tab");
  table("\x1bx", "Alt+x");
  table("\x1b\xE4\xB8\xAD", "Alt+\xE4\xB8\xAD");
  table("\x1b\r", "Alt+Enter");
  table("\x1b\x01", "Ctrl+Alt+a");
  table("\x1b", "Escape", true);
  table("\x1b\x1b", "Escape | Escape", true);
  table("\x1b[", "Escape | [", true);
  table("\x1b[<0;10;5M", "Mouse Press 1 @9,4");
  table("\x1b[<0;10;5m", "Mouse Release 1 @9,4");
  table("\x1b[<2;1;1M", "Mouse Press 3 @0,0");
  table("\x1b[<32;3;4M", "Mouse Drag 1 @2,3");
  table("\x1b[<35;3;4M", "Mouse Move @2,3");
  table("\x1b[<64;1;1M", "Mouse WheelUp @0,0");
  table("\x1b[<65;1;1M", "Mouse WheelDown @0,0");
  table("\x1b[<66;1;1M", "Mouse WheelLeft @0,0");    // a trackpad's sideways tick is never vertical scrolling
  table("\x1b[<67;1;1M", "Mouse WheelRight @0,0");
  table("\x1b[<70;3;4M", "Mouse Shift+WheelLeft @2,3");
  table("\x1b[<16;1;1M", "Mouse Ctrl+Press 1 @0,0");
  table("\x1b[<4;1;1M", "Mouse Shift+Press 1 @0,0");
  table("\x1b[200~hello\x1b[201~", "Paste(5 bytes)");
  table("\x1b[200~a\x1b" "b\x1b[201~x", "Paste(3 bytes) | x");
  table("\x1b[?1;2c", "Unknown(\x1b[?1;2c)");
  table("\x1b[99~", "Unknown(\x1b[99~)");
  table("a\x1b[Ab", "a | Up | b");

  // Split feeds: sequences cut at the read boundary complete on the next feed.
  {
    KeyDecoder d;
    std::vector<Event> a = d.feed("\x1b[");
    std::vector<Event> b = d.feed("A");
    check(a.empty() && names(b) == "Up", "CSI split across feeds completes");
    KeyDecoder e;
    std::vector<Event> c = e.feed("\xE4\xB8");
    std::vector<Event> f = e.feed("\xAD");
    check(c.empty() && names(f) == "\xE4\xB8\xAD", "UTF-8 split across feeds completes");
    KeyDecoder g;
    g.feed("\x1b[200~ab");
    check(g.in_paste(), "paste in progress is reported");
    std::vector<Event> h = g.feed("cd\x1b[20");
    std::vector<Event> i = g.feed("1~");
    check(h.empty() && names(i) == "Paste(4 bytes)", "paste terminator split across feeds");
    KeyDecoder j;
    j.feed("\x1b[200~xy");
    check(names(j.flush()) == "Paste(2 bytes)", "an unterminated paste is delivered on flush");
    KeyDecoder k;
    check(names(k.feed("\xFF")) == "\xEF\xBF\xBD", "an invalid byte is U+FFFD, not silence");
    KeyDecoder l;
    l.feed("\xF0\x9F");
    check(!l.feed("").size() && names(l.flush()) == "\xEF\xBF\xBD | \xEF\xBF\xBD", "a truncated sequence flushes as replacements");
  }
  {
    PasteEvent p;
    KeyDecoder d;
    for (const Event& e : d.feed("\x1b[200~line1\nline2\x1b[201~"))
      if (const PasteEvent* pe = std::get_if<PasteEvent>(&e)) p = *pe;
    check(p.text == "line1\nline2", "paste text is verbatim, newlines included");
  }
  return report("rolltui keys_test");
}
