#ifndef ROLLTUI_C_KEYS_H
#define ROLLTUI_C_KEYS_H
/*
 * rolltui/c/rolltui_keys.h — THE INPUT DECODER AND THE DELIVERABILITY MODEL, as C
 * (Phase 15 m3).
 *
 * A state machine over a byte buffer plus a pure classification of "would this chord ever
 * arrive?". Every rule, every protocol and every source is stated in `rolltui/Keys.hpp`
 * and asserted table by table in `rolltui/tests/keys_test.cpp` and
 * `rolltui/tests/deliverability_test.cpp`; none of it is repeated here, because the rules
 * are the same in both languages and a second copy is a second thing to drift.
 *
 * THE BOUNDARY'S RULES, all inherited from Phase 14 and none new:
 *   1. **THE CALLER OWNS EVERY BUFFER.** `rolltui_key_encode` fills one the caller sized
 *      from ROLLTUI_KEY_ENCODE_MAX; the decoder's own pending bytes are its handle's.
 *   2. **NOTHING IS RETURNED BY VALUE** from an `extern "C"` function.
 *   3. **TEXT OUT IS A BORROW WITH A STATED WINDOW.** An event's `text` — an Unknown
 *      key's raw bytes, or a paste's contents — points into the decoder's own storage and
 *      is valid for exactly the duration of the `emit` call it arrives in.
 *
 * ---- WHY A CHORD IS NOT A `KeyEvent`, AND WHY THAT IS NOT A SECOND DEFINITION ---------
 *
 * Phase 14 m2's rule is one definition for a struct that crosses. `RolltuiMouseEvent`
 * below obeys it outright: `rolltui::MouseEvent` IS this struct, methods and all.
 * `RolltuiChord` deliberately does not, and the reason is the finding rather than an
 * exception to it.
 *
 * `rolltui::KeyEvent` carries a `std::string raw` — the bytes of an Unknown sequence —
 * on a type that is otherwise a twelve-byte chord. **Nobody decided that; a string was
 * simply the easy place to put the bytes**, and it has cost something ever since: `raw`
 * takes part in the defaulted `operator==`, so every comparison site has to remember to
 * clear it first. `Bindings::action_for` did (`k.raw.clear()`), and two tests exist to
 * check that it still does. A C struct cannot hold a `std::string`, so the port had to
 * ask what actually crosses — and the answer is the CHORD. The payload travels in the
 * event envelope, where a variable-length borrow belongs, and the `.clear()` is gone
 * because dropping `raw` is now what the conversion IS rather than something a caller
 * must not forget.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the key vocabulary --------------------------------------------------------------- */
/* The same order as `rolltui::Key`, which is what lets the two be cast at the seam — and
 * the one place either language may. `ROLLTUI_KEY_COUNT` is asserted against
 * `rolltui::Key`'s own count in `Keys.cpp`, so a key added to one and not the other is a
 * compile error rather than a silently mis-indexed table. */
#define ROLLTUI_KEY_CHAR 0
#define ROLLTUI_KEY_ENTER 1
#define ROLLTUI_KEY_TAB 2
#define ROLLTUI_KEY_BACKSPACE 3
#define ROLLTUI_KEY_ESCAPE 4
#define ROLLTUI_KEY_UP 5
#define ROLLTUI_KEY_DOWN 6
#define ROLLTUI_KEY_LEFT 7
#define ROLLTUI_KEY_RIGHT 8
#define ROLLTUI_KEY_HOME 9
#define ROLLTUI_KEY_END 10
#define ROLLTUI_KEY_PAGEUP 11
#define ROLLTUI_KEY_PAGEDOWN 12
#define ROLLTUI_KEY_INSERT 13
#define ROLLTUI_KEY_DELETE 14
#define ROLLTUI_KEY_F1 15
#define ROLLTUI_KEY_F12 26
#define ROLLTUI_KEY_UNKNOWN 27
#define ROLLTUI_KEY_COUNT 28

/* One chord: a key, the character it is when the key is CHAR, and the three modifiers.
 * Never the raw bytes — see the note above. */
typedef struct RolltuiChord {
  unsigned char key ROLLTUI_DEFAULT(ROLLTUI_KEY_CHAR);
  RolltuiCodepoint ch ROLLTUI_DEFAULT(0);
  unsigned char ctrl ROLLTUI_DEFAULT(0), alt ROLLTUI_DEFAULT(0), shift ROLLTUI_DEFAULT(0);
} RolltuiChord;

/* ---- the mouse, defined ONCE and compiled by both languages ---------------------------- */
/* `rolltui::MouseEvent` IS this struct. `Kind` is spelled per language for the same reason
 * `Color::Kind` is (rolltui_style.h): C++ keeps the scoped enum twenty call sites already
 * write, C keeps the byte, and the underlying type is FIXED so the two are one byte by the
 * standard rather than by convention. */
typedef struct RolltuiMouseEvent {
#ifdef __cplusplus
  enum class Kind : unsigned char { Press = 0, Release, Drag, Move, WheelUp, WheelDown, WheelLeft, WheelRight };
  Kind kind = Kind::Press;
#else
  unsigned char kind; /* 0 press, 1 release, 2 drag, 3 move, 4-7 wheel up/down/left/right */
#endif
  int x ROLLTUI_DEFAULT(0), y ROLLTUI_DEFAULT(0); /* 0-based cells */
  int button ROLLTUI_DEFAULT(0);                  /* 1 left, 2 middle, 3 right; 0 for motion/wheel */
  unsigned char ctrl ROLLTUI_DEFAULT(0), alt ROLLTUI_DEFAULT(0), shift ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  constexpr bool operator==(const RolltuiMouseEvent&) const = default;
#endif
} RolltuiMouseEvent;

/* ---- what the decoder emits ------------------------------------------------------------ */

#define ROLLTUI_EVENT_KEY 0
#define ROLLTUI_EVENT_MOUSE 1
#define ROLLTUI_EVENT_PASTE 2

/* ONE event. `text` is a BORROW valid only for the `emit` call: an Unknown key's raw bytes
 * (kind KEY, key UNKNOWN) or a paste's contents (kind PASTE). It is NULL otherwise.
 * A resize is not decoded from bytes and so has no kind here — the Terminal makes it. */
typedef struct RolltuiEvent {
  unsigned char kind;
  RolltuiChord key;
  RolltuiMouseEvent mouse;
  const char* text;
  size_t text_len;
} RolltuiEvent;

/* Called once per event, in order. The C++ side appends to its `std::vector<Event>`. */
typedef void (*RolltuiEventFn)(void* ctx, const RolltuiEvent* e);

/* ---- the decoder ------------------------------------------------------------------------ */
/* OWNED, LONG-LIVED (CLAUDE.md's strategy 4): a decoder outlives every call and holds the
 * incomplete tail between them. `rolltui::KeyDecoder` owns exactly one. */
typedef struct RolltuiKeyDecoder RolltuiKeyDecoder;
RolltuiKeyDecoder* rolltui_key_decoder_new(void);
void rolltui_key_decoder_free(RolltuiKeyDecoder* d);

/* Consumes as much of `bytes` as forms complete events; the rest waits in the decoder. */
void rolltui_key_decoder_feed(RolltuiKeyDecoder* d, const char* bytes, size_t len, RolltuiEventFn emit, void* ctx);
/* Resolves whatever is pending: a lone ESC becomes Escape; an incomplete sequence becomes
 * Escape followed by its remaining bytes decoded as text. */
void rolltui_key_decoder_flush(RolltuiKeyDecoder* d, RolltuiEventFn emit, void* ctx);
int rolltui_key_decoder_pending(const RolltuiKeyDecoder* d);
int rolltui_key_decoder_in_paste(const RolltuiKeyDecoder* d);

/* ---- deliverability --------------------------------------------------------------------- */

#define ROLLTUI_PROTOCOL_LEGACY 0
#define ROLLTUI_PROTOCOL_MODIFY_OTHER_KEYS 1
#define ROLLTUI_PROTOCOL_KITTY 2

/* WHAT THE TERMINAL TURNED OUT TO BE — process-wide, and Legacy until something says
 * otherwise. It retains nothing, so it needs no shutdown releaser. */
unsigned char rolltui_key_active_protocol(void);
void rolltui_key_set_active_protocol(unsigned char p);

/* The bytes a terminal speaking `p` sends for this chord, into `out` (capacity `cap`,
 * at least ROLLTUI_KEY_ENCODE_MAX). Returns the length written, or -1 when `p` has no
 * encoding for it at all. Bytes may still be AMBIGUOUS — this is the encoding, not the
 * verdict. The longest any protocol produces is `CSI 27 ; mod ; code ~` with a six-digit
 * code, so sixteen is past every one of them and the cap is a constraint on this file
 * rather than on a caller's data. */
#define ROLLTUI_KEY_ENCODE_MAX 16
long rolltui_key_encode(const RolltuiChord* k, unsigned char p, char* out, size_t cap);

/* The verdict: `p` has bytes for this chord and they decode back to exactly it. */
int rolltui_key_deliverable(const RolltuiChord* k, unsigned char p);

/* WHY NOT, as a CODE rather than as words — the same split m2 made for a refused effect
 * registration. The four messages are English and belong where the words already are
 * (`Keys.cpp`), so the C classifies and never carries a sentence. */
#define ROLLTUI_UNDELIVERABLE_NONE 0     /* it is deliverable */
#define ROLLTUI_UNDELIVERABLE_NOT_A_KEY 1
#define ROLLTUI_UNDELIVERABLE_SHIFT_ON_CHAR 2
#define ROLLTUI_UNDELIVERABLE_NEEDS_ENHANCED 3 /* kitty, or xterm's modifyOtherKeys */
#define ROLLTUI_UNDELIVERABLE_NEEDS_KITTY 4
#define ROLLTUI_UNDELIVERABLE_NEVER 5
int rolltui_key_undeliverable_reason(const RolltuiChord* k, unsigned char p);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_KEYS_H */
