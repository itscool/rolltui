#pragma once
// rolltui/selftest/script.hpp — ONE script vocabulary for driving an app with no terminal.
//
// This is NOT part of the library's API and no product binary includes it. It exists so that a
// `<product>-selftest` binary can drive the app it links, and it lives here rather than in each
// app because three apps had written three dialects of it: a full parser, a partial
// re-implementation whose own comment called it "the studio's convention... only the spellings
// this app's own screen needs", and a third expressed as app-specific flags. Two consumers
// writing the same wrapper means the API is wrong, not the consumers.
//
// A step is an event plus the clock it happens at, so a scripted run is deterministic: the same
// script produces the same frame on any machine, which is what makes a golden frame a control
// rather than a recording of one afternoon.
//
// The vocabulary:
//   named keys           Up Down Left Right Home End PageUp PageDown Enter Escape Tab Backspace
//                        Delete, each with an optional Shift/Ctrl/Alt prefix (CtrlHome, AltEnter)
//   F1..F12              function keys
//   Type:hello_world     one key event per character; `_` is a space and `\_` a literal underscore
//   Paste:a\nb           ONE paste event, newlines included
//   Click x,y            press at a cell; ShiftClick, DblClick, TripleClick likewise
//   Drag x,y / Release   a held move and the end of it
//   WheelUp / WheelDown  over the middle of the screen
//   Tick                 a tick at the current clock, without an event
//   Tick:MS              a tick MS after the PREVIOUS step — the frame 60 ms into a slide is `Right Tick:60`
//
// `_` means space because an identifier is the thing you most often type into a name field, and
// a script is whitespace-separated so a literal space cannot appear in a token. **A PATH IS THE
// TRAP**: `/` passes through untouched so `Type:/tmp/x` is fine, but an underscore ANYWHERE in a
// typed path becomes a space and the path silently stops existing. Write `\_` for it.
#include "rolltui/rolltui.h"

// TWO INTERNAL HEADERS, and the reason is worth stating rather than hiding. Naming a key
// ("CtrlHome") and splitting a string into code points are both things a driver must do, and
// neither is on the public surface — so this header works for an app inside this repository and
// not for one outside it. That is a real limit on who can write a self-test binary, and the fix
// if it ever matters is to make those two public, not to copy them here.
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_unicode.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace rolltui_selftest {

struct Step {
  bool tick = false;
  RolltuiEvent ev{};
  std::uint64_t ms = 0;
  std::string owned_text;
};

std::vector<Step> scripted_keys(const std::string& spec, int w, int h) {
  std::vector<Step> out;
  std::istringstream in(spec);
  std::string tok;
  std::uint64_t clock = 1000;
  std::uint64_t last = 1000;  // the clock of the step most recently pushed: what `Tick:MS` counts from
  auto key_ev = [](unsigned char k, bool shift = false, bool ctrl = false, bool alt = false) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = k;
    e.key.shift = shift ? 1 : 0;
    e.key.ctrl = ctrl ? 1 : 0;
    e.key.alt = alt ? 1 : 0;
    return e;
  };
  auto ctrl_ev = [](char c) { RolltuiEvent e{}; e.kind = ROLLTUI_EVENT_KEY; e.key.key = ROLLTUI_KEY_CHAR; e.key.ch = static_cast<RolltuiCodepoint>(c); e.key.ctrl = 1; return e; };
  auto alt_ev = [](char c) { RolltuiEvent e{}; e.kind = ROLLTUI_EVENT_KEY; e.key.key = ROLLTUI_KEY_CHAR; e.key.ch = static_cast<RolltuiCodepoint>(c); e.key.alt = 1; return e; };
  // "Type:hello_world" → h e l l o ␠ w o r l d; "Paste:a\nb" → one paste event.
  auto unescape = [](std::string s, bool newlines) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
      // `\_` is a LITERAL underscore, the escape that `_`-means-space needs: an identifier is
      // the thing you most often want to type into a Name field, and without it
      // `Type:my_window` silently produces `mywindow`, because the Name spec drops the space.
      if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '_') { out.push_back('_'); ++i; }
      else if (s[i] == '_') out.push_back(' ');
      else if (newlines && s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out.push_back('\n'); ++i; }
      else out.push_back(s[i]);
    }
    return out;
  };
  // A named key with an optional Shift/Ctrl/Alt prefix: "ShiftLeft", "CtrlHome", "AltEnter".
  auto named = [&](std::string name, RolltuiEvent& out_ev) {
    bool shift = false, c = false, a = false;
    for (;;) {
      if (name.rfind("Shift", 0) == 0) { shift = true; name.erase(0, 5); }
      else if (name.rfind("Ctrl", 0) == 0) { c = true; name.erase(0, 4); }
      else if (name.rfind("Alt", 0) == 0) { a = true; name.erase(0, 3); }
      else break;
    }
    // The name -> Key table is `rolltui_key_from_display_name`, which reads the same
    // X-macro list the library's own `to_string` and chord parser do, case-insensitively.
    if (const int k = rolltui_key_from_display_name(name.data(), name.size()); k >= 0) {
      out_ev = key_ev(static_cast<unsigned char>(k), shift, c, a);
      return true;
    }
    if (name.size() == 1 && (c || a) && name[0] >= 'A' && name[0] <= 'Z') {  // CtrlA, AltC, ...
      out_ev = c ? ctrl_ev(static_cast<char>(name[0] - 'A' + 'a')) : alt_ev(static_cast<char>(name[0] - 'A' + 'a'));
      out_ev.key.shift = shift ? 1 : 0;
      return true;
    }
    return false;
  };
  auto mouse_ev = [](RolltuiMouseEvent::Kind k, int x, int y, int button = 1, bool shift = false) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_MOUSE;
    e.mouse.kind = k;
    e.mouse.x = x;
    e.mouse.y = y;
    e.mouse.button = button;
    e.mouse.shift = shift ? 1 : 0;
    return e;
  };
  auto push = [&](RolltuiEvent e, bool advance = true) {
    last = clock;
    out.push_back({false, e, clock, ""});
    if (advance) clock += 1000;
  };
  auto xy = [&](int& x, int& y) {
    std::string pos;
    if (!(in >> pos)) return false;
    std::size_t comma = pos.find(',');
    if (comma == std::string::npos) return false;
    x = std::atoi(pos.substr(0, comma).c_str());
    y = std::atoi(pos.substr(comma + 1).c_str());
    return true;
  };
  while (in >> tok) {
    int x = 0, y = 0;
    RolltuiEvent k{};
    if (tok == "Tick") { last = clock; out.push_back({true, {}, clock, ""}); }
    else if (tok.rfind("Tick:", 0) == 0) {
      clock = last + static_cast<std::uint64_t>(std::atoll(tok.substr(5).c_str()));
      last = clock;
      out.push_back({true, {}, clock, ""});
    } else if (tok.rfind("Type:", 0) == 0) {
      const std::string s = unescape(tok.substr(5), false);
      std::vector<RolltuiDecodedChar> chars(s.size());
      const std::size_t n = rolltui_u_decode_utf8_chars(s.data(), s.size(), chars.data());
      for (std::size_t i = 0; i < n; ++i) {
        RolltuiEvent e{};
        e.kind = ROLLTUI_EVENT_KEY;
        e.key.key = ROLLTUI_KEY_CHAR;
        e.key.ch = chars[i].cp;
        push(e, false);
      }
      clock += 1000;
    } else if (tok.rfind("Paste:", 0) == 0) {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_PASTE;
      last = clock;
      out.push_back({false, e, clock, unescape(tok.substr(6), true)});
      clock += 1000;
    } else if (named(tok, k)) push(k);
    else if (tok == "WheelUp" || tok == "WheelDown") {
      // Over the middle of the screen, which every built-in layout gives to the transcript.
      push(mouse_ev(tok == "WheelUp" ? RolltuiMouseEvent::Kind::WheelUp : RolltuiMouseEvent::Kind::WheelDown, w / 4, h / 3, 0));
    } else if (tok == "Click" || tok == "ShiftClick") {
      if (xy(x, y)) push(mouse_ev(RolltuiMouseEvent::Kind::Press, x, y, 1, tok == "ShiftClick"));
    } else if (tok == "DblClick" || tok == "TripleClick") {
      if (xy(x, y)) {
        const int presses = tok == "DblClick" ? 2 : 3;
        for (int i = 0; i < presses; ++i) {
          push(mouse_ev(RolltuiMouseEvent::Kind::Press, x, y), false);
          if (i + 1 < presses) push(mouse_ev(RolltuiMouseEvent::Kind::Release, x, y), false);
        }
        clock += 1000;
      }
    } else if (tok == "Drag") {
      if (xy(x, y)) push(mouse_ev(RolltuiMouseEvent::Kind::Drag, x, y));
    } else if (tok == "Release") {
      // Optional position; without one the release lands where the last event was.
      std::streampos here = in.tellg();
      std::string maybe;
      if (in >> maybe && maybe.find(',') != std::string::npos) {
        x = std::atoi(maybe.substr(0, maybe.find(',')).c_str());
        y = std::atoi(maybe.substr(maybe.find(',') + 1).c_str());
      } else {
        in.clear();
        in.seekg(here);
        const RolltuiMouseEvent* last = nullptr;
        for (const Step& s : out)
          if (s.ev.kind == ROLLTUI_EVENT_MOUSE) last = &s.ev.mouse;
        if (last) { x = last->x; y = last->y; }
      }
      push(mouse_ev(RolltuiMouseEvent::Kind::Release, x, y));
    } else {
      std::vector<RolltuiDecodedChar> chars(tok.size());
      const std::size_t n = rolltui_u_decode_utf8_chars(tok.data(), tok.size(), chars.data());
      if (n != 0) {
        RolltuiEvent e{};
        e.kind = ROLLTUI_EVENT_KEY;
        e.key.key = ROLLTUI_KEY_CHAR;
        e.key.ch = chars[0].cp;
        push(e);
      }
    }
  }
  return out;
}
}  // namespace rolltui_selftest
