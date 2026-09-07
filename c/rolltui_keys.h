#ifndef ROLLTUI_C_KEYS_H
#define ROLLTUI_C_KEYS_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_keys.h — THE INPUT DECODER AND THE DELIVERABILITY MODEL, as C
 *.
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
 * ---- WHY A CHORD IS NOT A `KeyEvent`, AND WHY THAT IS NOT A SECOND DEFINITION ---------------
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

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called once per event, in order. The C++ side appends to its `std::vector<Event>`. */
typedef void (*RolltuiEventFn)(void* ctx, const RolltuiEvent* e);

/* ---- the decoder ------------------------------------------------------------------------- */
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

long rolltui_key_encode(const RolltuiChord* k, unsigned char p, char* out, size_t cap);


/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached only by the library's own `.c` files and by a suite that tests this module's
 * implementation. The library does not promise these, so their shape can change without
 * breaking a consumer. A suite that needs one includes this header and names itself in
 * `ROLLTUI_INTERNAL_OPT_IN` (rolltui/CMakeLists.txt). */
/* Both spellings, BORROWED from static storage; `*len` may be NULL. An out-of-range ordinal
 * reads back as the CHAR row ("Char" / ""), never past the table. `rolltui_key_file_name`
 * returns "" (len 0) for `Char` and `Unknown`, which is what "this key has no chord name"
 * means — the keys editor refuses to bind exactly those. */
const char* rolltui_key_display_name(unsigned char key, size_t* len);
/* BORROWS a static literal; `*len` may be NULL. An out-of-range byte reads back as "legacy",
 * which is what `protocol_name`'s own default already was. */
const char* rolltui_key_protocol_name(unsigned char p, size_t* len);
void rolltui_key_set_active_protocol(unsigned char p);
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
/* The key of that DISPLAY name ("PageUp"), or -1. Case-insensitive, because `studio.cpp`'s
 * `--keys` scripts are typed by a person. The FILE-name direction already exists inside
 * `rolltui_chord_parse`, which is where a whole chord is spelled. */
int rolltui_key_from_display_name(const char* name, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_KEYS_H */
