#pragma once
//
// rolltui/Keys.hpp — the input event model and the pure `bytes → events` decoder.
//
// The decoder is a state machine over a byte buffer: feed() consumes complete
// sequences and keeps an incomplete tail (a lone ESC may be the start of a sequence
// or the Escape key — only time tells, so the Terminal calls flush() when a read
// times out and the tail becomes what it must then be). Recognised: UTF-8 text,
// C0 controls as Ctrl+letter, Enter/Tab/Backspace/Escape, CSI and SS3 cursor and
// function keys with xterm modifier parameters, Alt as ESC-prefix, SGR (1006) mouse
// reports, bracketed paste. Everything else in a well-formed CSI is reported as an
// Unknown key carrying the raw bytes, never silently dropped. Table-tested in
// rolltui/tests/keys_test.cpp.
//
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rolltui {

enum class Key : std::uint8_t {
  Char, Enter, Tab, Backspace, Escape, Up, Down, Left, Right, Home, End, PageUp, PageDown,
  Insert, Delete, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, Unknown
};

struct KeyEvent {
  Key key = Key::Char;
  char32_t ch = 0;      // for Char (and the letter for Ctrl+letter, lowercase)
  bool ctrl = false, alt = false, shift = false;
  std::string raw;      // for Unknown: the bytes
  bool operator==(const KeyEvent&) const = default;
};

struct MouseEvent {
  enum class Kind : std::uint8_t { Press, Release, Drag, Move, WheelUp, WheelDown };
  Kind kind = Kind::Press;
  int x = 0, y = 0;     // 0-based cells
  int button = 0;       // 1 left, 2 middle, 3 right; 0 for motion/wheel
  bool ctrl = false, alt = false, shift = false;
  bool operator==(const MouseEvent&) const = default;
};

struct PasteEvent {
  std::string text;
  bool operator==(const PasteEvent&) const = default;
};

struct ResizeEvent {
  int w = 0, h = 0;
  bool operator==(const ResizeEvent&) const = default;
};

using Event = std::variant<KeyEvent, MouseEvent, PasteEvent, ResizeEvent>;

class KeyDecoder {
 public:
  // Consumes as much of `bytes` as forms complete events; the rest waits.
  std::vector<Event> feed(std::string_view bytes);
  // Resolves whatever is pending: a lone ESC becomes Escape; an incomplete sequence
  // becomes Escape followed by its remaining bytes decoded as text.
  std::vector<Event> flush();
  bool pending() const { return !buf_.empty(); }
  bool in_paste() const { return in_paste_; }

 private:
  std::string buf_;
  bool in_paste_ = false;
  std::string paste_;
};

// Debug/text form of an event ("Ctrl+c", "Up", "Mouse Press 1 @3,4", …).
std::string to_string(const Event& e);

}  // namespace rolltui
