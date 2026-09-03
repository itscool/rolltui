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
#include <string>
#include <vector>

#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "rolltui/Bindings.hpp"
#include "rolltui/Keys.hpp"
#include "rolltui/Terminal.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

KeyEvent key(Key k, bool ctrl = false, bool alt = false, bool shift = false) {
  KeyEvent e;
  e.key = k;
  e.ctrl = ctrl;
  e.alt = alt;
  e.shift = shift;
  return e;
}
KeyEvent ch(char32_t c, bool ctrl = false, bool alt = false, bool shift = false) {
  KeyEvent e;
  e.key = Key::Char;
  e.ch = c;
  e.ctrl = ctrl;
  e.alt = alt;
  e.shift = shift;
  return e;
}

// What this library ACTUALLY makes of those bytes. flush() is part of it: a lone ESC is
// only the Escape key once nothing follows, which is exactly what Terminal::poll does on
// a timeout.
std::vector<Event> decode(const std::string& bytes) {
  KeyDecoder d;
  std::vector<Event> ev = d.feed(bytes);
  const std::vector<Event> rest = d.flush();
  ev.insert(ev.end(), rest.begin(), rest.end());
  return ev;
}

// The round trip: does the protocol's encoding of this chord come back as this chord?
// This is the ground truth the rule is measured against — not a second copy of the rule.
bool arrives_as_itself(const KeyEvent& k, KeyProtocol p) {
  const std::optional<std::string> bytes = encode_key(k, p);
  if (!bytes || bytes->empty()) return false;
  const std::vector<Event> ev = decode(*bytes);
  if (ev.size() != 1) return false;
  const KeyEvent* got = std::get_if<KeyEvent>(&ev[0]);
  if (!got) return false;
  KeyEvent bare = *got;
  bare.raw.clear();
  return bare == k;
}

std::string show(const KeyEvent& k) { return chord_to_string(k); }

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
std::vector<KeyEvent> universe() {
  std::vector<KeyEvent> all;
  static const Key kKeys[] = {Key::Enter,  Key::Tab,   Key::Backspace, Key::Escape, Key::Up,   Key::Down,
                              Key::Left,   Key::Right, Key::Home,      Key::End,    Key::PageUp, Key::PageDown,
                              Key::Insert, Key::Delete, Key::F1,       Key::F2,     Key::F3,   Key::F4,
                              Key::F5,     Key::F6,    Key::F7,        Key::F8,     Key::F9,   Key::F10,
                              Key::F11,    Key::F12};
  const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789 !@#$%^&*()-_=+[]{}\\|;:'\",.<>/?`~";
  for (int m = 0; m < 8; ++m) {
    const bool sh = m & 1, al = m & 2, ct = m & 4;
    for (Key k : kKeys) all.push_back(key(k, ct, al, sh));
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
    TerminalOptions opts;
    opts.handle_signals = false;  // this is a test process, not an application
    Terminal t(pty.slave, pty.slave, opts);
    p = t.key_protocol();
  }
  ::close(pty.slave);
  int status = 0;
  ::waitpid(pid, &status, 0);
  return p;
}

}  // namespace

int main() {
  // ---- 1. THE TABLE, AGAINST THE ENCODINGS ------------------------------------------
  // The rule and the bytes, over the whole universe. Any disagreement names the chord,
  // the protocol, the bytes and both verdicts.
  {
    const KeyProtocol protocols[] = {KeyProtocol::Legacy, KeyProtocol::ModifyOtherKeys, KeyProtocol::Kitty};
    int checked = 0;
    for (const KeyEvent& k : universe())
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
    for (const KeyEvent& k : universe()) {
      if (deliverable(k, KeyProtocol::Legacy) && !deliverable(k, KeyProtocol::ModifyOtherKeys)) { monotone = false; broke = show(k); }
      if (deliverable(k, KeyProtocol::ModifyOtherKeys) && !deliverable(k, KeyProtocol::Kitty)) { monotone = false; broke = show(k); }
    }
    check(monotone, "Legacy ⊆ ModifyOtherKeys ⊆ Kitty: an enhanced protocol never loses a chord [" + broke + "]");
  }

  // ---- 3. THE ROWS THE MILESTONE IS ABOUT, NAMED ------------------------------------
  {
    const KeyEvent ctrl_shift_p = ch(U'p', true, false, true);
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
    const KeyEvent ctrl_shift_left = key(Key::Left, true, false, true);
    check(deliverable(ctrl_shift_left, KeyProtocol::Legacy) &&
              encode_key(ctrl_shift_left, KeyProtocol::Legacy) == std::string("\x1b[1;6D"),
          "ctrl+shift+left IS deliverable on a plain terminal: CSI 1;6D [xterm ctlseqs; kitty legacy functional table]");
    check(deliverable(key(Key::F12, true, true, true), KeyProtocol::Legacy),
          "…and so is ctrl+alt+shift+f12: every combination, on every functional key");

    // The other side of the same coin: Enter/Tab/Backspace/Escape are NOT functional
    // keys and carry almost nothing.
    check(!deliverable(key(Key::Enter, false, false, true), KeyProtocol::Legacy) &&
              !deliverable(key(Key::Enter, true), KeyProtocol::Legacy) &&
              deliverable(key(Key::Enter, false, true), KeyProtocol::Legacy),
          "shift+enter and ctrl+enter are legacy-undeliverable (all three are 0x0D); alt+enter is ESC CR and works");
    check(deliverable(key(Key::Enter, false, false, true), KeyProtocol::Kitty) &&
              encode_key(key(Key::Enter, false, false, true), KeyProtocol::Kitty) == std::string("\x1b[13;2u"),
          "…and kitty tells Shift+Enter from Enter: CSI 13;2u [kitty functional key codes: ENTER 13]");
    check(deliverable(key(Key::Tab, false, false, true), KeyProtocol::Legacy) &&
              encode_key(key(Key::Tab, false, false, true), KeyProtocol::Legacy) == std::string("\x1b[Z"),
          "shift+tab is the exception on Tab: back-tab, CSI Z — which is why stack.focus_prev may use it");
    check(!deliverable(key(Key::Tab, true), KeyProtocol::Legacy) && !deliverable(key(Key::Escape, false, true), KeyProtocol::Legacy),
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
              undeliverable_reason(ch(U'p', false, false, true), KeyProtocol::Kitty).find("shifted character") != std::string::npos,
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
    std::optional<Bindings> b = Bindings::from_json(file, legacy, KeyProtocol::Legacy);
    check(b && !legacy.clean() && legacy.undeliverable.size() == 1 &&
              legacy.undeliverable[0] ==
                  "app.palette: 'ctrl+shift+p' cannot be delivered by this terminal; it needs the kitty keyboard protocol",
          "the loader names the reason [" + (legacy.undeliverable.empty() ? "" : legacy.undeliverable[0]) + "]");
    check(b && legacy.bad_chords.empty(), "…as its own kind of problem, not as 'not a chord': it parses perfectly");

    // A TERMINAL THAT DOES NEGOTIATE KITTY TAKES THE SAME FILE WITH NO COMPLAINT. This
    // is the assertion that proves the answer is the terminal's and not a blacklist:
    // one file, one loader, two verdicts, decided only by the protocol argument.
    BindingsLoadReport kitty;
    std::optional<Bindings> k = Bindings::from_json(file, kitty, KeyProtocol::Kitty);
    check(k && kitty.clean(), "…and the SAME FILE loads clean under kitty [" + kitty.summary() + "]");

    // Kept, not dropped: today's terminal is not tomorrow's, and a bindings file is the
    // user's. The row round-trips through save; what changes is that nothing emits it.
    check(b && b->chords_for("app.palette").size() == 1,
          "a refused chord is KEPT in the table, so `bindings save` never eats it");
    BindingsLoadReport rt;
    std::optional<Bindings> back = Bindings::from_json(b->to_json("p"), rt, KeyProtocol::Kitty);
    check(back && rt.clean() && back->chords_for("app.palette").size() == 1,
          "…and comes back alive on a terminal that can deliver it, with no edit to the file");

    // Inert, and invisible with it.
    set_active_key_protocol(KeyProtocol::Legacy);
    b->declare({{"app.palette", "the palette"}});
    check(b->action_for(ch(U'p', true, false, true), "app").empty(),
          "…while on this terminal it answers no key: kept and inert, exactly as an undeclared action's row is");
    check(b->chords_text("app.palette").empty() && !b->chords_for("app.palette").empty(),
          "…and the HELP form drops it while the table keeps it: a shortcut printed is a promise the key works");
    set_active_key_protocol(KeyProtocol::Kitty);
    check(b->action_for(ch(U'p', true, false, true), "app") == "app.palette" && b->chords_text("app.palette") == "Ctrl-Shift-P",
          "…and on kitty the very same table answers the key and prints the shortcut");
    set_active_key_protocol(KeyProtocol::Legacy);

    // Neither of the two mercy rungs may become an undeliverability refusal by accident.
    BindingsLoadReport other;
    std::optional<Bindings> o = Bindings::from_json(
        R"({"name":"o","bindings":{"input.submit":["enter"],"other.thing":["f9"],"playground.quit":["ctrl+q"]}})", other, KeyProtocol::Legacy);
    check(o && other.clean() && other.migrated.size() == 1 && o->chords_for("studio.quit").size() == 1 &&
              o->chords_for("other.thing").size() == 1,
          "another screen's action is still kept and a renamed one still migrated: deliverability touches neither");
  }

  // ---- 5. THE SHIPPED FILE IS DELIVERABLE EVERYWHERE ---------------------------------
  // default_bindings() aborts the build if it is not — asserted here so the failure has
  // a name as well as an exit status, the way Phase 11 m1's tool-row abort is.
  {
    BindingsLoadReport rep;
    std::optional<Bindings> d = Bindings::from_json(default_bindings_json(), rep, KeyProtocol::Legacy);
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
