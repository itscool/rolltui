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
/* ---- the key vocabulary, in BOTH its spellings (Phase 17 m2b, 2026-09-05) ------------------
 *
 * THE SIXTH VOCABULARY GAP OF THIS PHASE, and the same shape as the first five. The key names
 * existed in FIVE places across TWO spellings:
 *   lowercase — `rolltui_bindings.c`'s `kKeyNames` ("enter", "pageup"): what a bindings FILE
 *               says, with the aliases a user may type and a canonical flag saying which one
 *               `chord_to_string` prints back;
 *   TitleCase — `Keys.cpp`'s `names[]` ("Enter", "PageUp"): what `to_string(Event)` PRINTS,
 *               hand-copied into `keys_test.cpp`, `terminal_test.cpp` and `studio.cpp:1340`
 *               (a name -> Key map for its `--keys` scripts, re-derived because a host could
 *               not reach the library's).
 *
 * **THE TWO SPELLINGS ARE DELIBERATE AND NOTHING ANYWHERE SAID SO**, which is the actual
 * defect: failure shape 1 (two spellings of one identity) sitting on top of shape 3 (relocated
 * into a caller that cannot reach it). They are deliberate because they answer two different
 * questions — a FILE's word is lowercase and stable, a HUMAN's is TitleCase and readable — so
 * the fix is not to pick one. It is to put both in one list, next to each other, where adding
 * a key is a single edit and a divergence is impossible to introduce silently.
 *
 * THREE COLUMNS: the C constant's suffix, the TitleCase display name, and the lowercase file
 * name — "" for the two keys a bindings file has no word for (`Char` is the character itself
 * and `Unknown` is bytes that decoded to nothing).
 *
 * ORDER IS ABI, same as `ROLLTUI_ROLE_LIST`: the ordinal crosses in every `RolltuiChord`. */
#define ROLLTUI_KEY_LIST(X) \
  X(CHAR,      "Char",      "") \
  X(ENTER,     "Enter",     "enter") \
  X(TAB,       "Tab",       "tab") \
  X(BACKSPACE, "Backspace", "backspace") \
  X(ESCAPE,    "Escape",    "escape") \
  X(UP,        "Up",        "up") \
  X(DOWN,      "Down",      "down") \
  X(LEFT,      "Left",      "left") \
  X(RIGHT,     "Right",     "right") \
  X(HOME,      "Home",      "home") \
  X(END,       "End",       "end") \
  X(PAGEUP,    "PageUp",    "pageup") \
  X(PAGEDOWN,  "PageDown",  "pagedown") \
  X(INSERT,    "Insert",    "insert") \
  X(DELETE,    "Delete",    "delete") \
  X(F1,        "F1",        "f1") \
  X(F2,        "F2",        "f2") \
  X(F3,        "F3",        "f3") \
  X(F4,        "F4",        "f4") \
  X(F5,        "F5",        "f5") \
  X(F6,        "F6",        "f6") \
  X(F7,        "F7",        "f7") \
  X(F8,        "F8",        "f8") \
  X(F9,        "F9",        "f9") \
  X(F10,       "F10",       "f10") \
  X(F11,       "F11",       "f11") \
  X(F12,       "F12",       "f12") \
  X(UNKNOWN,   "Unknown",   "")

/* The ALIASES a bindings file also accepts and `chord_to_string` never prints. "space" is the
 * odd one and stays odd: it names a CHAR chord (U+0020), not a key, so it carries its
 * codepoint rather than only an ordinal. */
#define ROLLTUI_KEY_ALIAS_LIST(X) \
  X("esc",   ESCAPE,   0) \
  X("pgup",  PAGEUP,   0) \
  X("pgdn",  PAGEDOWN, 0) \
  X("del",   DELETE,   0) \
  X("space", CHAR,     ' ')

/* Both spellings, BORROWED from static storage; `*len` may be NULL. An out-of-range ordinal
 * reads back as the CHAR row ("Char" / ""), never past the table. `rolltui_key_file_name`
 * returns "" (len 0) for `Char` and `Unknown`, which is what "this key has no chord name"
 * means — the keys editor refuses to bind exactly those. */
const char* rolltui_key_display_name(unsigned char key, size_t* len);
const char* rolltui_key_file_name(unsigned char key, size_t* len);

/* The key of that DISPLAY name ("PageUp"), or -1. Case-insensitive, because `studio.cpp`'s
 * `--keys` scripts are typed by a person. The FILE-name direction already exists inside
 * `rolltui_chord_parse`, which is where a whole chord is spelled. */
int rolltui_key_from_display_name(const char* name, size_t len);

typedef enum RolltuiKey {
#define ROLLTUI_KEY_ENUM_(UPPER, Title, file) ROLLTUI_KEY_##UPPER,
  ROLLTUI_KEY_LIST(ROLLTUI_KEY_ENUM_)
#undef ROLLTUI_KEY_ENUM_
  ROLLTUI_KEY_COUNT
} RolltuiKey;

/* The enum above already defines ROLLTUI_KEY_F1..F12; several files index that range
 * arithmetically (`ROLLTUI_KEY_F1 + n`), which the contiguity of the list guarantees. */

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

/* THE THREE PROTOCOL NAMES (Phase 17 m2a). `rolltui_terminal.h` says these "are vocabulary
 * that stays C++", and `Keys.cpp`'s own header comment says the same — right while the library
 * was C++ with a C core, wrong once the library IS the C, because `Keys.cpp` is deleted in m2c
 * and the words would go with it. It had already reached a second spelling in
 * `deliverability_test.cpp:84`. Same reversal as the roles, the effect states and the depth
 * names; ORDER IS ABI (`rolltui_key_active_protocol` hands back the byte). */
#define ROLLTUI_PROTOCOL_LIST(X) \
  X("legacy", LEGACY, Legacy) \
  X("modifyOtherKeys", MODIFY_OTHER_KEYS, ModifyOtherKeys) \
  X("kitty", KITTY, Kitty)

typedef enum RolltuiKeyProtocol {
#define ROLLTUI_PROTOCOL_ENUM_(lower, UPPER, Camel) ROLLTUI_PROTOCOL_##UPPER,
  ROLLTUI_PROTOCOL_LIST(ROLLTUI_PROTOCOL_ENUM_)
#undef ROLLTUI_PROTOCOL_ENUM_
  ROLLTUI_PROTOCOL_COUNT
} RolltuiKeyProtocol;

/* BORROWS a static literal; `*len` may be NULL. An out-of-range byte reads back as "legacy",
 * which is what `protocol_name`'s own default already was. */
const char* rolltui_key_protocol_name(unsigned char p, size_t* len);

/* The protocol of that name, or -1. CASE-INSENSITIVE, because "modifyOtherKeys" is the one
 * name in this library with an interior capital and a config file should not have to know. */
int rolltui_key_protocol_from_name(const char* name, size_t len);

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

/* WHY NOT, as a CODE — and, since Phase 17 m2a, as WORDS too. This comment used to read "the
 * four messages are English and belong where the words already are (`Keys.cpp`), so the C
 * classifies and never carries a sentence." The split was right; the destination was not, and
 * it cost the usual: `deliverability_test.cpp:110`, `bindings_test.cpp:180` and
 * `menu_test.cpp:273` had each hand-copied the sentences, because `Keys.cpp` is where they
 * were and a test asserting on one has to say it. With `Keys.cpp` deleted in m2c they would
 * have had no original at all — three copies and no source. */
#define ROLLTUI_UNDELIVERABLE_NONE 0     /* it is deliverable */
#define ROLLTUI_UNDELIVERABLE_NOT_A_KEY 1
#define ROLLTUI_UNDELIVERABLE_SHIFT_ON_CHAR 2
#define ROLLTUI_UNDELIVERABLE_NEEDS_ENHANCED 3 /* kitty, or xterm's modifyOtherKeys */
#define ROLLTUI_UNDELIVERABLE_NEEDS_KITTY 4
#define ROLLTUI_UNDELIVERABLE_NEVER 5
int rolltui_key_undeliverable_reason(const RolltuiChord* k, unsigned char p);

/* The sentence for that code. BORROWS a static literal; `*len` may be NULL. Code 0 (it IS
 * deliverable) and any out-of-range code read back as "" — there is nothing to say. */
const char* rolltui_key_undeliverable_text(int code, size_t* len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_KEYS_H */
