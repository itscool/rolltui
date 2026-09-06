#ifndef ROLLTUI_C_KEYS_H
#define ROLLTUI_C_KEYS_H
/* INTERNAL since Phase 19 m2: the public declarations of this module live in
 * `rolltui/rolltui.h`, the library's one definition; what is below is the library's own —
 * reached by the library's own .c files and by a test that opts in by including this file by name. */
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

#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

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

long rolltui_key_encode(const RolltuiChord* k, unsigned char p, char* out, size_t cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_KEYS_H */
