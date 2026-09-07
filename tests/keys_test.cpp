//
// keys_test.cpp — the bytes → events table for KeyDecoder, incl. split feeds, the
// lone-ESC flush, SGR mouse and bracketed paste.
//
#include <cstddef>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"

/* INTERNAL headers, BY NAME. This file is not a CONSUMER: the studio and its editors are
 * rolltui's own authoring tool for rolltui's own files, and a suite that tests implementation
 * opts in by listing itself in ROLLTUI_INTERNAL_OPT_IN (rolltui/CMakeLists.txt). */
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_keys.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_unicode.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"

using namespace rolltui_test;

namespace {

// event_to_string mirrors rolltui::to_string(Event) (rolltui/Keys.cpp) one level down,
// directly over RolltuiEvent. The COMPOSITION (modifier prefixes, the mouse form) is still
// this file's; the KEY NAMES are the library's now.
std::string event_to_string(const RolltuiEvent& e) {
  // THE LIBRARY'S TitleCase names, not a hand-copy of them. There were three
  // copies of this 28-entry table and no source: the lowercase half was already in C, the
  // TitleCase half was in `Keys.cpp`, and nothing said the two spellings were deliberate.
  static const char* mouse_kinds[] = {"Press", "Release", "Drag", "Move", "WheelUp", "WheelDown", "WheelLeft", "WheelRight"};
  switch (e.kind) {
    case ROLLTUI_EVENT_MOUSE: {
      std::string s = "Mouse ";
      if (e.mouse.ctrl) s += "Ctrl+";
      if (e.mouse.alt) s += "Alt+";
      if (e.mouse.shift) s += "Shift+";
      s += mouse_kinds[static_cast<int>(e.mouse.kind)];
      if (e.mouse.button) s += " " + std::to_string(e.mouse.button);
      return s + " @" + std::to_string(e.mouse.x) + "," + std::to_string(e.mouse.y);
    }
    case ROLLTUI_EVENT_PASTE:
      return "Paste(" + std::to_string(e.text_len) + " bytes)";
    default: {
      std::string s;
      if (e.key.ctrl) s += "Ctrl+";
      if (e.key.alt) s += "Alt+";
      if (e.key.shift) s += "Shift+";
      if (e.key.key == ROLLTUI_KEY_CHAR) {
        char buf[4];
        const std::size_t n = rolltui_u_append_utf8(e.key.ch, buf);
        s.append(buf, n);
      } else {
        s += rolltui_key_display_name(static_cast<unsigned char>(e.key.key), nullptr);
      }
      if (e.key.key == ROLLTUI_KEY_UNKNOWN) s += "(" + std::string(e.text ? e.text : "", e.text ? e.text_len : 0) + ")";
      return s;
    }
  }
}

// The sink the decoder emits through: one event appended per call, with the borrowed
// bytes copied HERE, inside the window the boundary states, and nowhere else — the
// same shape as rolltui/Keys.cpp's own `collect`, just formatting instead of building
// a variant.
void collect(void* ctx, const RolltuiEvent* e) {
  static_cast<std::vector<std::string>*>(ctx)->push_back(event_to_string(*e));
}

void capture_paste(void* ctx, const RolltuiEvent* e) {
  if (e->kind == ROLLTUI_EVENT_PASTE) *static_cast<std::string*>(ctx) = std::string(e->text, e->text_len);
}

std::vector<std::string> feed_events(RolltuiKeyDecoder* d, const std::string& bytes) {
  std::vector<std::string> out;
  rolltui_key_decoder_feed(d, bytes.data(), bytes.size(), collect, &out);
  return out;
}

std::vector<std::string> flush_events(RolltuiKeyDecoder* d) {
  std::vector<std::string> out;
  rolltui_key_decoder_flush(d, collect, &out);
  return out;
}

std::string names(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += (s.empty() ? "" : " | ") + x;
  return s;
}

void table(const std::string& bytes, const std::string& want, bool flush_after = false) {
  RolltuiKeyDecoder* d = rolltui_key_decoder_new();
  std::vector<std::string> ev = feed_events(d, bytes);
  if (flush_after) {
    std::vector<std::string> more = flush_events(d);
    ev.insert(ev.end(), more.begin(), more.end());
  }
  rolltui_key_decoder_free(d);
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
  // LF is Ctrl-J, not Enter. Terminal always runs raw — cfmakeraw clears
  // ICRNL and INLCR — so Enter is CR and the only thing that sends LF is ctrl+j. While
  // both are Enter, `ctrl+j` is a chord that parses, binds, saves, renders in the help popup
  // and never fires — and a shipped fixture binds exactly that chord, so the defect is live
  // and advertised in a golden frame.
  table("\n", "Ctrl+j");
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
  // ESC + an UPPERCASE letter is alt+shift+<letter>: the one multi-modifier chord the
  // legacy encoding carries (kitty's spec names shift+alt as one of exactly two).
  // Canonical chords are lowercase with a shift flag, so the decoder folds it that way.
  table("\x1b" "B", "Alt+Shift+b");
  table("\x1b\x1b[Z", "Escape | Shift+Tab");  // not a letter: ESC still starts a sequence

  // ---- the two ENHANCED forms ----
  // kitty: CSI unicode-key-code ; modifiers u, the code ALWAYS the unshifted key, the
  // modifiers 1 + (shift 1 | alt 2 | ctrl 4).  [sw.kovidgoyal.net/kitty/keyboard-protocol]
  table("\x1b[112;6u", "Ctrl+Shift+p");   // the chord the whole milestone is named for
  table("\x1b[97;6u", "Ctrl+Shift+a");    // the spec's own example: 97, never 65
  table("\x1b[13;5u", "Ctrl+Enter");
  table("\x1b[13;2u", "Shift+Enter");     // "your terminal can't tell Shift+Enter from Enter"
  table("\x1b[9;5u", "Ctrl+Tab");
  table("\x1b[127;5u", "Ctrl+Backspace");
  table("\x1b[27u", "Escape");            // the disambiguate flag's whole point
  table("\x1b[91;5u", "Ctrl+[");          // legacy sends 0x1B here, which is Escape
  table("\x1b[47;5u", "Ctrl+/");          // legacy has no control code for it at all
  table("\x1b[97:65;6:1u", "Ctrl+Shift+a");  // sub-parameters: shifted key, event type — ignored
  table("\x1b[57399;1u", "Unknown(\x1b[57399;1u)");  // kitty's private-use keypad range
  // xterm modifyOtherKeys: CSI 27 ; modifier ; keycode ~, the keycode UNSHIFTED.
  // [invisible-island.net/xterm/modified-keys.html — its own examples are \e[27;5;9~
  //  control-TAB, \e[27;5;44~ control-comma, \e[27;5;47~ control-slash]
  table("\x1b[27;5;9~", "Ctrl+Tab");
  table("\x1b[27;5;44~", "Ctrl+,");
  table("\x1b[27;5;47~", "Ctrl+/");
  table("\x1b[27;5;112~", "Ctrl+p");
  table("\x1b[27;7;112~", "Ctrl+Alt+p");
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
    RolltuiKeyDecoder* d = rolltui_key_decoder_new();
    std::vector<std::string> a = feed_events(d, "\x1b[");
    std::vector<std::string> b = feed_events(d, "A");
    check(a.empty() && names(b) == "Up", "CSI split across feeds completes");
    rolltui_key_decoder_free(d);
    RolltuiKeyDecoder* e = rolltui_key_decoder_new();
    std::vector<std::string> c = feed_events(e, "\xE4\xB8");
    std::vector<std::string> f = feed_events(e, "\xAD");
    check(c.empty() && names(f) == "\xE4\xB8\xAD", "UTF-8 split across feeds completes");
    rolltui_key_decoder_free(e);
    RolltuiKeyDecoder* g = rolltui_key_decoder_new();
    feed_events(g, "\x1b[200~ab");
    check(rolltui_key_decoder_in_paste(g) != 0, "paste in progress is reported");
    std::vector<std::string> h = feed_events(g, "cd\x1b[20");
    std::vector<std::string> i = feed_events(g, "1~");
    check(h.empty() && names(i) == "Paste(4 bytes)", "paste terminator split across feeds");
    rolltui_key_decoder_free(g);
    RolltuiKeyDecoder* j = rolltui_key_decoder_new();
    feed_events(j, "\x1b[200~xy");
    check(names(flush_events(j)) == "Paste(2 bytes)", "an unterminated paste is delivered on flush");
    rolltui_key_decoder_free(j);
    RolltuiKeyDecoder* k = rolltui_key_decoder_new();
    check(names(feed_events(k, "\xFF")) == "\xEF\xBF\xBD", "an invalid byte is U+FFFD, not silence");
    rolltui_key_decoder_free(k);
    RolltuiKeyDecoder* l = rolltui_key_decoder_new();
    feed_events(l, "\xF0\x9F");
    check(!feed_events(l, "").size() && names(flush_events(l)) == "\xEF\xBF\xBD | \xEF\xBF\xBD", "a truncated sequence flushes as replacements");
    rolltui_key_decoder_free(l);
  }
  {
    std::string p;
    RolltuiKeyDecoder* d = rolltui_key_decoder_new();
    const std::string bytes = "\x1b[200~line1\nline2\x1b[201~";
    rolltui_key_decoder_feed(d, bytes.data(), bytes.size(), capture_paste, &p);
    rolltui_key_decoder_free(d);
    check(p == "line1\nline2", "paste text is verbatim, newlines included");
  }
  // ---- the two rows the shared key list needs a word for ------------------------------
  // `Char` and `Unknown` are in the shared key list with "" as their FILE spelling (a bindings
  // file has no word for either), and "space" is the one ALIAS that names a CHAR chord and so
  // carries a codepoint. The second check is a real control for that: spelling the alias
  // "spacebar" in the list turns it and two `bindings_test` assertions red. The FIRST is a
  // true statement that is NOT a control — `rolltui_chord_parse` refuses an empty last part
  // upstream, so the empty-name guard beside the table is belt and braces; that file says so.
  {
    RolltuiChord c{};
    check(rolltui_chord_parse("", 0, &c) == 0, "an empty chord is not a chord (the empty key spelling is not a name)");
    RolltuiChord space{};
    char buf[ROLLTUI_CHORD_STRING_MAX];
    check(rolltui_chord_parse("space", 5, &space) && space.key == ROLLTUI_KEY_CHAR && space.ch == U' ' &&
              std::string(buf, rolltui_chord_to_string(&space, buf, sizeof buf)) == "space",
          "…while 'space', which IS an alias for a Char chord, still round-trips");
  }

  return report("rolltui keys_test");
}
