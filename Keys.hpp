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
// reports, bracketed paste, and — since Phase 12 m3 — the two ENHANCED forms: kitty's
// `CSI code ; mod u` and xterm's modifyOtherKeys `CSI 27 ; mod ; code ~`.
// Everything else in a well-formed CSI is reported as an
// Unknown key carrying the raw bytes, never silently dropped. Table-tested in
// rolltui/tests/keys_test.cpp.
//
// LF (0x0A) IS CTRL-J, NOT ENTER (corrected in Phase 12 m3). Terminal always runs raw
// — cfmakeraw clears ICRNL and INLCR — so Enter arrives as CR (0x0D) and the only thing
// that sends LF is ctrl+j. Reporting both as Enter made `ctrl+j` a chord that parses,
// binds, saves and shows in the help popup and never fires: this milestone's defect one
// layer below the terminal. The Phase 10 files-only fixture binds exactly that chord and
// its golden frame advertises "Ctrl-J", which is how it was found.
//
// PHASE 15 m3 — THE DECODER AND THE DELIVERABILITY MODEL ARE BEHIND A C BOUNDARY
// (`rolltui/c/rolltui_keys.h`), in one of two implementations chosen by `-DROLLTUI_C`
// (`KeysCpp.cpp` or `c/rolltui_keys.c`). This header is the vocabulary, the C++ event
// model and the shape every widget already writes against; nothing a caller does changed.
// Two things one level down are worth knowing:
//   - `MouseEvent` IS the C struct (Phase 14 m2's one-definition rule), so its `ctrl` /
//     `alt` / `shift` are `unsigned char` rather than `bool` — the same trade
//     `rolltui_style.h` records for a Style's attribute bits, and for the same reason.
//   - `KeyEvent` is NOT, and the reason is a finding rather than an exception: `raw` is a
//     `std::string` on a type that is otherwise a chord, it takes part in the defaulted
//     `operator==`, and every comparison site had to remember to clear it first. The
//     boundary carries the CHORD and the bytes travel in the event envelope, so the
//     `.clear()` that `Bindings::action_for` used to do is now what the conversion IS.
//     See `rolltui/c/rolltui_keys.h` for the whole of it.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "rolltui/c/rolltui_keys.h"

namespace rolltui {

enum class Key : std::uint8_t {
  Char, Enter, Tab, Backspace, Escape, Up, Down, Left, Right, Home, End, PageUp, PageDown,
  Insert, Delete, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, Unknown
};
// The two spellings of the key vocabulary, pinned to each other by the compiler rather
// than by a comment: a key added to one and not the other stops the build instead of
// mis-indexing a table at the seam.
static_assert(static_cast<int>(Key::Char) == ROLLTUI_KEY_CHAR && static_cast<int>(Key::F1) == ROLLTUI_KEY_F1 &&
                  static_cast<int>(Key::F12) == ROLLTUI_KEY_F12 &&
                  static_cast<int>(Key::Unknown) == ROLLTUI_KEY_UNKNOWN && ROLLTUI_KEY_UNKNOWN + 1 == ROLLTUI_KEY_COUNT,
              "rolltui::Key and the C key constants must be the same vocabulary in the same order");

struct KeyEvent {
  Key key = Key::Char;
  char32_t ch = 0;      // for Char (and the letter for Ctrl+letter, lowercase)
  bool ctrl = false, alt = false, shift = false;
  std::string raw;      // for Unknown: the bytes
  bool operator==(const KeyEvent&) const = default;
};

// WheelLeft/WheelRight are xterm's buttons 6/7 (SGR codes 66/67): a trackpad's sideways
// ticks. They are their own kinds so a host cannot mistake them for vertical scrolling —
// the terminal has already split the gesture into per-axis ticks, so there are no deltas
// to apply a dead zone to; a host that does not scroll sideways simply ignores them.
// (Found 2026-09-01: both decoded as WheelDown, so a sideways drag scrolled down and a
// gesture starting sideways at the top jumped the wrong way.)
//
// ONE DEFINITION: this is `RolltuiMouseEvent` (rolltui/c/rolltui_keys.h), not a copy of
// it, so the decoder writes exactly the bytes a widget reads.
using MouseEvent = RolltuiMouseEvent;

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
  // OWNED (CLAUDE.md's fourth strategy), through a `unique_ptr` with a deleter that calls
  // the C free — one owner, structural lifetime, no hand-rolled `delete`. The same shape
  // `Frame` uses for its handle. A decoder is not copied: it holds the bytes that have
  // arrived and not yet been decoded, and two of those would be two half-read terminals.
  struct Handle {
    void operator()(RolltuiKeyDecoder* p) const { rolltui_key_decoder_free(p); }
  };
  KeyDecoder() : d_(rolltui_key_decoder_new()) {}

  // Consumes as much of `bytes` as forms complete events; the rest waits.
  std::vector<Event> feed(std::string_view bytes);
  // Resolves whatever is pending: a lone ESC becomes Escape; an incomplete sequence
  // becomes Escape followed by its remaining bytes decoded as text.
  std::vector<Event> flush();
  bool pending() const { return rolltui_key_decoder_pending(d_.get()) != 0; }
  bool in_paste() const { return rolltui_key_decoder_in_paste(d_.get()) != 0; }

 private:
  std::unique_ptr<RolltuiKeyDecoder, Handle> d_;
};

// Debug/text form of an event ("Ctrl+c", "Up", "Mouse Press 1 @3,4", …).
std::string to_string(const Event& e);

// THE CHORD A KeyEvent IS, and back. `raw` is DROPPED by the first and empty in the second,
// which is what makes "a KeyEvent's raw bytes are never part of a chord" structural instead
// of a `.clear()` every comparison site has to remember (rolltui/c/rolltui_keys.h).
RolltuiChord chord_of(const KeyEvent& k);
KeyEvent key_event_of(const RolltuiChord& c);

// ---- DELIVERABILITY (Phase 12 m3) ---------------------------------------------------
//
// THE DEFECT THIS EXISTS FOR: `ctrl+shift+p` parses, binds, saves, and shows in the help
// popup — and on an ordinary terminal it never fires, because the bytes that arrive are
// byte-identical to `ctrl+p`. Correct in every observable way except that it does
// nothing. CLAUDE.md's Explicit-over-implicit section, in the key layer.
//
// THE RULE, and it is the whole model:
//
//   A chord is DELIVERABLE under a protocol when that protocol has a byte sequence for
//   it that this decoder turns back into exactly that chord. Deliverability is a
//   property of the PAIR (chord, protocol) and never of the chord alone.
//
// So there is no blacklist of chords anywhere in this library: `encode_key` states what
// a terminal speaking each protocol SENDS, and `deliverable` is the round trip through
// the real KeyDecoder. rolltui/tests/deliverability_test.cpp asserts exactly that
// equivalence over an enumerated universe of ~800 chords × 3 protocols, so the rule
// below is checked against the ENCODINGS rather than restated.
//
// WHAT EACH PROTOCOL CARRIES, with its source:
//
//   Legacy — what every terminal does with no negotiation, and the conservative default.
//     * The functional keys carry ANY modifier combination, because xterm already
//       parameterises them: `CSI 1 ; mod {ABCDEFHPQS}` and `CSI n ; mod ~`, mod = 1 +
//       (shift 1 | alt 2 | ctrl 4). Up/Down/Left/Right/Home/End/Insert/Delete/PageUp/
//       PageDown/F1-F12 are therefore fully bindable — `ctrl+shift+left` included, which
//       is the row a guessed blacklist gets wrong.
//       [xterm ctlseqs, "CSI 1 ; modifier"; kitty keyboard-protocol, legacy functional
//        key table: UP = "1 A", DELETE = "3 ~", F5 = "15 ~"]
//     * Tab carries shift, and only shift, via back-tab `CSI Z`.
//     * Enter, Backspace and Escape carry alt only, as an ESC prefix. Nothing carries
//       ctrl or shift on them: Enter, Shift+Enter and Ctrl+Enter are all 0x0D.
//       [blog.fsck.com, "Your Terminal Can't Tell Shift+Enter from Enter"]
//     * A character carries ctrl only where an ASCII control code exists for it, and
//       alt as an ESC prefix. ctrl+h/ctrl+i/ctrl+m/ctrl+[/ctrl+@/ctrl+? are the codes
//       that ALREADY belong to Backspace/Tab/Enter/Escape/ctrl+space/Backspace, so they
//       are ambiguous and refused; ctrl+j is not (LF is nobody else's — see the note at
//       the top of this file).
//     * NOTHING carries shift on a character. The keyboard layout folds shift into the
//       character before the terminal ever sees a keysym, so `shift+p` is the character
//       'P' and there is no bit left to say otherwise.
//       [kitty keyboard-protocol, "Problems with the legacy keyboard protocol": "No way
//        to reliably use multiple modifier keys, other than shift+alt and ctrl+alt"]
//
//   ModifyOtherKeys — xterm's `CSI > 4 ; 2 m`, reporting `CSI 27 ; mod ; code ~`.
//     It ADDS ctrl and alt to the keys that had no way to carry them: every character
//     including punctuation, and Enter/Tab/Backspace/Escape. It adds NOTHING to shift.
//     [invisible-island.net/xterm/modified-keys.html, whose only concrete examples are
//      `\e[27;5;9~` control-TAB, `\e[27;5;44~` control-comma, `\e[27;5;47~` control-slash
//      — all UNSHIFTED codes. The same page declines to say which keysym it reports when
//      Shift is held ("the key symbol from the LookupString functions", i.e. the shifted
//      one, while the modifier parameter separately carries shift) and calls the
//      interaction "hard for users to follow, without testing". Unverified means refused:
//      the reason then names kitty, where the spec IS unambiguous.]
//
//   Kitty — `CSI > 1 u` (the disambiguate flag), reporting `CSI code ; mod u` where the
//     code is ALWAYS the unshifted codepoint. It carries every combination on every key,
//     the functional keys keeping their legacy CSI form. The only thing still not a
//     chord is bare shift on a character, which is text in every protocol.
//     [sw.kovidgoyal.net/kitty/keyboard-protocol: "CSI unicode-key-code ; modifiers u";
//      ctrl+shift+a is `CSI 97;6u`, not 65. Enter/Tab/Backspace keep their legacy bytes
//      UNMODIFIED so a wedged shell can still be reset; modified, they are `CSI 13;5u`
//      and friends.]
//
enum class KeyProtocol : std::uint8_t { Legacy, ModifyOtherKeys, Kitty };

std::string_view protocol_name(KeyProtocol p);              // "legacy" | "modifyOtherKeys" | "kitty"
std::optional<KeyProtocol> parse_key_protocol(std::string_view name);

// WHAT THE TERMINAL TURNED OUT TO BE. Terminal::negotiate_keyboard() sets it once at
// startup and again on a terminal change; everything that has to answer "would this
// chord ever arrive?" without a Terminal in reach reads it here. It is Legacy until
// something says otherwise — an unknown terminal, a pipe, a `--frame` run and a
// terminal that answers nothing all get the CONSERVATIVE model, never the permissive
// one, because a wrong Legacy answer refuses a key that would have worked while a wrong
// Kitty answer silently accepts a key that never fires.
KeyProtocol active_key_protocol();
void set_active_key_protocol(KeyProtocol p);

// The bytes a terminal speaking `p` sends for this key press, or nullopt when `p` has no
// encoding for it at all. Bytes may still be AMBIGUOUS — encode_key({Char 'm', ctrl},
// Legacy) is "\r", which is Enter's — so this is the encoding, not the verdict.
std::optional<std::string> encode_key(const KeyEvent& k, KeyProtocol p);

// The verdict: `p` has bytes for this chord and they decode back to exactly it.
bool deliverable(const KeyEvent& k, KeyProtocol p);

// Why not, as the loader says it: "it needs the kitty keyboard protocol". "" when the
// chord is deliverable. The reason names the CHEAPEST protocol that would carry it, so
// a person is told what to turn on rather than that something is impossible.
std::string undeliverable_reason(const KeyEvent& k, KeyProtocol p);

}  // namespace rolltui
