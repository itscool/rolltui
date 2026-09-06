#ifndef ROLLTUI_H
#define ROLLTUI_H
/*
 * rolltui.h — THE library's one public header, and since Phase 19 m2 (2026-09-06) THE
 * DEFINITION: every public type is defined here and nowhere else, every public function is
 * declared here, and the vocabulary those signatures speak — the role list, the key list, the
 * caps, the enums — is here. `#include "rolltui/rolltui.h"` is the whole API.
 *
 * The headers under `rolltui/c/` are the library's own. They include this file first and hold
 * what the library's own `.c` files need beyond it: the internal structs, the steps of public operations, the
 * hooks the library registers itself. DON'T include one from a host. A unit test of an
 * internal may, by naming the header it needs and being listed in `rolltui/CMakeLists.txt`'s
 * opt-in list; `public_header_test` asserts that no other consumer does.
 *
 * WHY IT IS A DEFINITION AND NOT AN INCLUDE LIST. Until Phase 19 this file was an umbrella
 * over 37 headers, 851 functions, 62% of them reached by no host or tool — the whole library
 * with an include list on top, chosen by what consumers had reached for, which cannot tell an
 * entry point from plumbing. The user's intent, stated before Phase 17 and re-captured on
 * 2026-09-06: "intentional public api design", "delete some headers that don't expose anything
 * needed even internally", "it was supposed to literally define it instead of including
 * headers". `rolltui/tests/api_classes.inc` is that intent as data: every function's class
 * (PUBLIC, TOOL_FACING, INTERNAL) with its reason, held to measured reach by
 * `public_header_test`, and this file is written FROM that table — the public and tool-facing
 * rows and the types their signatures need — never as a prediction of it.
 *
 * ---- AND WHAT KEEPS THAT TABLE HONEST, because it has been got wrong three times ------------
 *
 * **REACH IS THE INPUT; A STATED REASON IS THE DECISION. DON'T use "who reaches it" as the
 * criterion** — it is the cheapest signal available, which is why this repo has reached for it
 * three times and had to retract it three times:
 *   1. **Phase 17 m4** derived the public HEADER set from what consumers reached for → 37 of 39
 *      headers public → retracted by m4b: *"measuring reach cannot tell an entry point from
 *      plumbing, because a consumer reaching THROUGH a bad API looks identical to one reaching
 *      FOR a good one."*
 *   2. **Phase 17 m5(f)** asked *"is anything dead?"* counting the library's own `.c` as a
 *      consumer → 2 symbols → corrected when Phase 19 was scoped: the question was whether a
 *      CONSUMER needs it, not whether anything at all touches it.
 *   3. **Phase 19 m1** classified per function with a clause making a thing public when *"a test
 *      stands in for a host to exercise"* it → **304 of 624 public functions in front of no
 *      consumer at all** → struck by Phase 20. **A TEST'S REACH IS NEVER A REASON: a test exists
 *      to reach things.**
 * **DO let a PUBLIC-ONLY SUITE's reach count** — a program shaped like a CONSUMER, which includes
 * this header and nothing else (`rolltui/tests/c_consumer_test.c` above all, and every one of
 * roll's own tests, roll being a host). That is not a test's reach standing in for a consumer's;
 * it is a consumer-shaped program's. **DO write `KEPT: <why>` on a row that is public for a
 * reason rather than for reach** — the count is asserted, so adding one is a decision a reader
 * can audit rather than a comment nobody re-reads.
 *
 * ============================================================================
 * THE RULES EVERY DECLARATION BELOW OBEYS — as DO/DON'T, because a maxim has to be decoded
 * before it can be acted on and a directive can be followed.
 * ============================================================================
 *   1. HANDLES. DO create and release in a pair: `rolltui_x_new(...)` / `rolltui_x_free(...)`,
 *      and `free` is a no-op on NULL. DON'T expect RAII from C; DO call `rolltui_shutdown()`
 *      at the end and assert `live_bytes == 0 && live_blocks == 0` (`rolltui_mem_stats`) —
 *      that is how a missed release is caught rather than hoped about.
 *   2. RETURNS. DON'T return anything BY VALUE from an `extern "C"` function. DO fill a
 *      buffer the caller owns and reuses.
 *   3. TEXT OUT has exactly three shapes. DO pick by the rule, not by taste:
 *        (a) BOUNDED — `size_t f(…, char* out, size_t cap)` when the maximum is known and
 *            NAMED (`ROLLTUI_SGR_MAX`, `ROLLTUI_CHORD_STRING_MAX`, …): a caller declares
 *            `char buf[ROLLTUI_SGR_MAX]` and allocates nothing.
 *        (b) UNBOUNDED — `void f(…, RolltuiStr* out)`, REPLACING a growing buffer the caller
 *            owns and reuses, when no maximum exists.
 *        (c) BORROWED — `const char* f(…, size_t* len)`, memory the library keeps, with the
 *            window stated on that function's own line. Never yours to free.
 *   4. WORKING MEMORY is a handle the caller owns (`RolltuiDrawScratch`, `RolltuiWrapScratch`,
 *      …): DO make one per thread, reuse it, free it. DON'T let a callee invent storage.
 *   4b. MANY THINGS OUT has one shape, the same as 3(b): a list the caller owns and reuses,
 *      REPLACED on every call (`RolltuiStrList`, `RolltuiPresetList`, `RolltuiRows`). DON'T
 *      hand a result back through a callback.
 *   5. CALLBACKS cross as {function pointer, void* ctx, void (*free_ctx)(void*)} and DO carry
 *      only a DECISION into the library. DON'T ship a lambda bridge from here.
 *
 * ============================================================================
 * TOOL-FACING SECTIONS (Phase 19 m4). Three sections below carry `[TOOL-FACING]` in their
 * banner — theme_analysis, theme_gen, undo — and hold the 42 functions only an EDITOR reaches:
 * the studio's theme checks, the generator, the undo stack. They are in THIS file and not in a
 * second one, because a second public header is a second include list and the phase declined
 * one twice. DO include this header for an editor exactly as for a host. DON'T reach for a
 * function in a marked section from a host — a host that needs one has found a gap, and the
 * fix is to move the function out (its class changes with it). DON'T declare a function a
 * host reaches inside a marked section. `public_header_test` section 8 holds both directions:
 * every TOOL_FACING row of the class table is declared under a marked banner, and no PUBLIC
 * row is.
 *
 * C++ consumers
 * ============================================================================
 * roll, the studio, paint and the editors are C++ and call this header directly. The structs
 * below carry C++ members under `__cplusplus`, and the rule for them is the user's
 * (2026-09-06): a member exists to support C++ AS A LANGUAGE, maybe; a member that binds
 * every consumer to one C++ API a consumer may not want is lock-in of another kind and makes
 * every other binding harder.
 *   - DON'T add a `__cplusplus` member that names a `std::` container or view. rolltui's own
 *     types and the C standard's only; `public_header_test` section 7 asserts it.
 *   - DO convert to `std::string` in YOUR OWN file, at the site that wants it, and never on a
 *     frame (`tests/status_budget_test.cpp` is the instrument).
 *   - COPY IS DELETED on every owning struct. DO spell a copy `x.clone()` (which is
 *     `rolltui_x_copy`) or `a.assign(b)` (which is `rolltui_str_set`). `RolltuiLayer copy = *p;`
 *     was a deep copy here and a shallow alias in C — Phase 16 m6's double-free.
 *   - MOVES stay (each is `rolltui_x_move` as a member) and DESTRUCTORS stay: RAII is the
 *     language's ownership model, the named `_free` is public and the C consumer proves the
 *     pair, so a destructor hides nothing.
 *   - If two consumers write the SAME wrapper, the API is wrong, not the consumers — and the
 *     fix goes into rolltui's OWN vocabulary, never into adopting theirs. That has fired five
 *     times (the double buffer, the action table, `RolltuiPresetInfo`, `push_popup`, the
 *     preset domains); each time the fix was C. When every host needs an operation, that is
 *     a GAP: DO investigate and choose between PROVIDING it (in C) and REMOVING THE NEED;
 *     DON'T pick either by reflex, and DON'T forbid either by rule.
 */

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
extern "C" {
#endif

/* ========================================================================================
 * abi — the ABI macros every declaration below uses, and the code point
 * ======================================================================================== */
/* `static inline` and the null pointer, spelled once — a header that carries a shared
 * definition for both languages needs both, and `NULL` is not `nullptr` in C++'s eyes. */
#define ROLLTUI_INLINE inline

#ifdef __cplusplus
#define ROLLTUI_NULL nullptr
#define ROLLTUI_DEFAULT(v) = v
#define ROLLTUI_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
typedef char32_t RolltuiCodepoint;
#endif
#ifndef __cplusplus
#define ROLLTUI_NULL ((void*)0)
#define ROLLTUI_DEFAULT(v)
#define ROLLTUI_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
typedef unsigned int RolltuiCodepoint;

#endif
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiCodepoint) == 4,
                      "a code point must be the same four-byte unsigned scalar in both languages");


/* ========================================================================================
 * str — RolltuiStr and RolltuiStrList: the text vocabulary every signature speaks
 * ======================================================================================== */
typedef struct RolltuiStr {
  char* p ROLLTUI_DEFAULT(nullptr); /* NUL-terminated when non-NULL; NULL is the empty string */
  size_t n ROLLTUI_DEFAULT(0);      /* bytes, not counting the NUL */
  size_t cap ROLLTUI_DEFAULT(0);    /* bytes allocated, including room for the NUL */

#ifdef __cplusplus
  // THE C++ SHAPE, cut to the user's rule (Phase 19 m2, 2026-09-06): a member may name rolltui's
  // own types and the C standard's, never a std:: container or view. What a host wants as a
  // std::string it converts in its own file, at the site that wants it. COPY IS DELETED:
  // `RolltuiStr a = b;` was a deep copy in C++ and a shallow alias in C — Phase 16 m6's
  // double-free, one type over — and the one spelling is `a.assign(b)`, which is
  // `rolltui_str_set`. MOVE stays: it is `rolltui_str_move` as a member. THE DESTRUCTOR STAYS:
  // RAII is the language's ownership model, `rolltui_str_free` is public and the C consumer
  // proves the pair, so it hides nothing. Every other member is one C function with `this`.
  RolltuiStr() = default;
  RolltuiStr(const RolltuiStr&) = delete;
  RolltuiStr& operator=(const RolltuiStr&) = delete;
  RolltuiStr(RolltuiStr&& o) noexcept : p(o.p), n(o.n), cap(o.cap) { o.p = nullptr; o.n = o.cap = 0; }
  RolltuiStr& operator=(RolltuiStr&& o) noexcept;
  RolltuiStr(const char* s) { assign(s); }  // NOLINT(google-explicit-constructor)
  ~RolltuiStr();
  RolltuiStr& operator=(const char* s) { assign(s); return *this; }
  void assign(const char* s, std::size_t len);
  void assign(const char* s) { assign(s, s ? std::strlen(s) : 0); }
  void assign(const RolltuiStr& o) { assign(o.p, o.n); }
  void append(const char* s, std::size_t len);
  void append(const RolltuiStr& o) { append(o.p, o.n); }
  RolltuiStr& operator+=(const char* s) { append(s, s ? std::strlen(s) : 0); return *this; }
  RolltuiStr& operator+=(char c) { append(&c, 1); return *this; }
  const char* c_str() const { return p ? p : ""; }
  const char* data() const { return p ? p : ""; }
  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  void clear();  // keeps the buffer — the reuse this type exists for
  bool eq(const char* s, std::size_t len) const;
  bool operator==(const char* o) const { return eq(o, o ? std::strlen(o) : 0); }
  bool operator==(const RolltuiStr& o) const { return eq(o.p, o.n); }
#endif
} RolltuiStr;

/* Replaces the contents. `s` may be NULL only when `len` is 0. Keeps the buffer when it
 * already fits, which is what makes a re-assigned name free after the first. */
void rolltui_str_set(RolltuiStr* s, const char* text, size_t len);
/* Appends. GROWING EXACT is still the right strategy: a name is built from a handful of
 * pieces, not a stream (that is what the markdown store's pools are for). */
void rolltui_str_append(RolltuiStr* s, const char* text, size_t len);
void rolltui_str_append_str(RolltuiStr* s, const RolltuiStr* o);
/* Empties WITHOUT releasing — `clear()` as a reset was shipped four times in Phase 13 and
 * is the one this file will not repeat (CLAUDE.md's design lens). */
void rolltui_str_clear(RolltuiStr* s);
/* Releases the buffer and zeroes the struct. Safe on a zeroed struct and on NULL. */
void rolltui_str_free(RolltuiStr* s);
/* Takes `from`'s buffer, releasing whatever `to` held. `from` is left empty and owning
 * nothing — the move the C++ side spells with `&&`, written down so the C has it too. */
void rolltui_str_move(RolltuiStr* to, RolltuiStr* from);
int rolltui_str_eq(const RolltuiStr* s, const char* text, size_t len);
/* A BORROW of the bytes, never NULL: the empty string reads back as "" with `*len` 0, so a
 * caller never branches on NULL to print a name. */
const char* rolltui_str_get(const RolltuiStr* s, size_t* len);

typedef struct RolltuiPtrVec {
  void** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);
} RolltuiPtrVec;


/* ---- the string SINK, for any function whose result is N strings ---------------------------
 * One `put` call per string, into whatever the caller is collecting. It lives here because
 * this is the string module; it was declared in `rolltui_presets.h` until Phase 17 m3, when
 * `rolltui_menu.h`'s tree walks needed the same shape and writing it twice would have been the
 * duplication rule firing. `s` is a BORROW valid for the call only. */
typedef void (*RolltuiPutFn)(void* ctx, const char* s, size_t len);

/* MANY STRINGS OUT, into a buffer the caller owns and reuses — the same shape `RolltuiStr` is
 * for ONE string, one dimension up, and the answer to the question `rolltui.h` rule 3 had for
 * text and did not have for lists (Phase 17 m4b). Zero-initialise; `_release` frees everything
 * and zeroes it; in C++ the destructor does that. `_add` appends a copy and returns a BORROW of
 * the stored entry, valid until the next `_add`. */
typedef struct RolltuiStrList {
  RolltuiStr* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiStrList() = default;
  RolltuiStrList(const RolltuiStrList&) = delete;
  RolltuiStrList& operator=(const RolltuiStrList&) = delete;
  ~RolltuiStrList();
  const RolltuiStr* begin() const { return v; }
  const RolltuiStr* end() const { return v + n; }
  size_t size() const { return n; }
  bool empty() const { return n == 0; }
  const RolltuiStr& operator[](size_t i) const { return v[i]; }
#endif
} RolltuiStrList;

void rolltui_str_list_release(RolltuiStrList* l);
void rolltui_str_list_clear(RolltuiStrList* l); /* n = 0; every entry's buffer is KEPT for reuse */
RolltuiStr* rolltui_str_list_add(RolltuiStrList* l, const char* s, size_t len);

/* THE BRIDGE from the sink shape to the buffer shape, so a caller who wants a `RolltuiStr`
 * out of a function that still takes a `RolltuiPutFn` writes no lambda: pass this as `put`
 * and the `RolltuiStr*` as `ctx`. APPENDS (it does not clear), so a multi-`put` walk
 * concatenates; clear the target first if that is not what you want.
 *
 * IT IS A BRIDGE AND NOT A BLESSING OF THE SINK SHAPE (Phase 17 m4b): a function whose result
 * the library ALREADY HAS takes the `RolltuiStr*` (one string) or the `RolltuiStrList*` (many)
 * directly, and every public one that took a sink was converted. `RolltuiPutFn` survives as an
 * INTERNAL plumbing shape — the library streams into its own `Buf` through it — and as the
 * type of the descriptor hooks a DOMAIN supplies, which is a decision going IN and not a result
 * coming out. `public_header_test` asserts no public function hands a result back through it. */
void rolltui_str_put(void* ctx, const char* s, size_t len);

#ifdef __cplusplus
namespace rolltui {
// The two names the rest of the library writes. `Str` is a value with one owner; `PtrVec`
// is deliberately NOT wrapped in RAII here, because every user of it owns elements of a
// different type and the FREEING of those elements is the thing that must stay visible at
// the owner (a layout node frees child nodes, `Windows` frees widgets). A RAII wrapper
// would have to take a deleter, which is a `std::function` at a place this port exists to
// remove one from.
using Str = RolltuiStr;
using PtrVec = RolltuiPtrVec;

}  // namespace rolltui
#endif
#ifdef __cplusplus
inline RolltuiStr::~RolltuiStr() { rolltui_str_free(this); }
inline RolltuiStrList::~RolltuiStrList() { rolltui_str_list_release(this); }
inline void RolltuiStr::assign(const char* s, std::size_t len) { rolltui_str_set(this, s, len); }
inline void RolltuiStr::append(const char* s, std::size_t len) { rolltui_str_append(this, s, len); }
inline bool RolltuiStr::eq(const char* s, std::size_t len) const { return rolltui_str_eq(this, s, len) != 0; }
inline void RolltuiStr::clear() { rolltui_str_clear(this); }
inline RolltuiStr& RolltuiStr::operator=(RolltuiStr&& o) noexcept {
  if (this != &o) rolltui_str_move(this, &o);
  return *this;
}

#endif

/* ========================================================================================
 * keys — the chord and protocol vocabulary a host shows
 * ======================================================================================== */
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


/* WHAT THE TERMINAL TURNED OUT TO BE — process-wide, and Legacy until something says
 * otherwise. It retains nothing, so it needs no shutdown releaser. */
unsigned char rolltui_key_active_protocol(void);

/* The bytes a terminal speaking `p` sends for this chord, into `out` (capacity `cap`,
 * at least ROLLTUI_KEY_ENCODE_MAX). Returns the length written, or -1 when `p` has no
 * encoding for it at all. Bytes may still be AMBIGUOUS — this is the encoding, not the
 * verdict. The longest any protocol produces is `CSI 27 ; mod ; code ~` with a six-digit
 * code, so sixteen is past every one of them and the cap is a constraint on this file
 * rather than on a caller's data. */
#define ROLLTUI_KEY_ENCODE_MAX 16





/* ========================================================================================
 * bindings — a host holds a bindings table and its report
 * ======================================================================================== */
/* "ctrl+shift+left", "alt+enter", "f1", "escape", "?", "space" — in any modifier order,
 * case-insensitive. 1 on success. A chord never carries an Unknown key's raw bytes: the
 * boundary takes the chord, which is what makes that structural (rolltui_keys.h). */
int rolltui_chord_parse(const char* text, size_t len, RolltuiChord* out);

/* The canonical spelling, and the help form. "" for an Unknown key. The longest either
 * produces is three modifiers plus the longest key name, so thirty-two is past every one of
 * them and the cap is a constraint on this file rather than on a caller's data. */
#define ROLLTUI_CHORD_STRING_MAX 32
size_t rolltui_chord_to_string(const RolltuiChord* k, char* out, size_t cap);
size_t rolltui_chord_display(const RolltuiChord* k, char* out, size_t cap);

/* ---- the table --------------------------------------------------------------------------- */
/* OWNED, LONG-LIVED (CLAUDE.md's strategy 4): one per `rolltui::Bindings`, which frees it.
 *
 * THREE PARALLEL LISTS, exactly as the C++ had: the DECLARED actions with their
 * descriptions, and the ROWS (action → chords). They are separate because a row may outlive
 * a declaration — a bindings file is global and the user's, so a row for another screen's
 * action is KEPT and inert (Bindings.hpp), which is only expressible if "has a row" and "is
 * declared" are two questions. */
typedef struct RolltuiBindings RolltuiBindings;
RolltuiBindings* rolltui_bindings_new(void);
void rolltui_bindings_free(RolltuiBindings* b);
RolltuiBindings* rolltui_bindings_clone(const RolltuiBindings* b);
/* Equal by the ROWS alone, which is what the C++ `operator==` compared: a declaration is
 * this screen's, a row is the user's file, and only the second is the domain's content. */
int rolltui_bindings_equal(const RolltuiBindings* a, const RolltuiBindings* b);


/* A no-op when the action is already declared. Creates an empty ROW when there is none —
 * and never touches a row that exists, because declaring is what makes a kept row live and
 * must not throw its chords away. */
size_t rolltui_bindings_action_count(const RolltuiBindings* b);
/* A BORROW, valid until the table next changes. */
const char* rolltui_bindings_action_at(const RolltuiBindings* b, size_t i, size_t* len);
int rolltui_bindings_has(const RolltuiBindings* b, const char* action, size_t len);

/* Answers whether a scope is one the library defines — the caller's fact, asked for by
 * `rolltui_bindings_undeclare_others` below. */
typedef int (*RolltuiScopeFn)(void* ctx, const char* scope, size_t len);


size_t rolltui_bindings_chord_count(const RolltuiBindings* b, const char* action, size_t len);

/* The HELP spelling: every chord bound to `action` that THIS TERMINAL can deliver, in display
 * form ("Ctrl-W, Alt-Backspace"), comma-separated. CLEARS `out`. The undeliverable filter is
 * what makes this the library's and not a loop a caller writes — it had three independent
 * implementations before Phase 17 m2a, the third written by an agent that could reach neither
 * of the other two. */
void rolltui_bindings_chords_text(const RolltuiBindings* b, const char* action, size_t alen, RolltuiStr* out);
/* Chord `i` of the row, into `out`. 0 when there is none. */
int rolltui_bindings_chord_at(const RolltuiBindings* b, const char* action, size_t len, size_t i, RolltuiChord* out);
/* The action of `scope` this chord serves, or NULL: a row that nothing declares never
 * answers, and neither does a chord the ACTIVE protocol cannot deliver — both are kept in
 * the table and written back, so neither may claim a key. A BORROW, as above. */
const char* rolltui_bindings_action_for(const RolltuiBindings* b, const RolltuiChord* k, const char* scope,
                                        size_t scope_len, size_t* out_len);

/* Binds `chord`; a chord already bound to another action of the same scope MOVES, and
 * `moved_from` (a BORROW, valid until the next change) says which. Refused (0) for an
 * undeclared action and for a chord the Enter rule protects. */
int rolltui_bindings_bind(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord,
                          const char** moved_from, size_t* moved_len);
int rolltui_bindings_unbind(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord);
/* Pushes a chord onto a row with NO rule checking, creating the row if there is none. The
 * loader's own, and separate from `bind` on purpose: a file's conflicts, its Enter rule and
 * its undeliverable chords are all REPORTED with a message before anything lands, so the
 * loader has already decided and needs a put rather than a policy. */
void rolltui_bindings_clear(RolltuiBindings* b, const char* action, size_t len);

/* The scope of an action name: "input" of "input.submit", the whole name when there is no
 * dot. A BORROW of the caller's own bytes. */
const char* rolltui_bindings_scope_of(const char* action, size_t len, size_t* out_len);

/* THE REPORT, transparent like `RolltuiAppProfileReport`: exactly `RolltuiStr` values in
 * GROWING AMORTISED arrays, one per `BindingsLoadReport` field. Zero-initialise before use. */
typedef struct RolltuiBindingsReport {
  RolltuiStr error; /* non-empty: unusable, and rolltui_bindings_load_json leaves `b` untouched */
  RolltuiStr* unknown_actions;
  size_t unknown_actions_n, unknown_actions_cap;
  RolltuiStr* bad_chords;
  size_t bad_chords_n, bad_chords_cap;
  /* Chords this terminal cannot deliver: kept in `b` and reported, never dropped. */
  RolltuiStr* undeliverable;
  size_t undeliverable_n, undeliverable_cap;
  RolltuiStr* conflicts;
  size_t conflicts_n, conflicts_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
} RolltuiBindingsReport;

void rolltui_bindings_report_release(RolltuiBindingsReport* r); /* frees everything; zeroes it */
int rolltui_bindings_report_clean(const RolltuiBindingsReport* r);
/* Mirrors `BindingsLoadReport::summary()` exactly: "" when clean, else `error`, else
 * "bad: x; conflict: y; chord: z; undeliverable: w; unknown action: u; unknown: k" joined in
 * that order. Replaces `*out`. */

/* `RolltuiReasonFn`-shaped, over `rolltui_key_undeliverable_reason`/`_text` (rolltui_keys.h) —
 * the same story one function over: the reason text has been C since Phase 15, and the only
 * thing keeping a host from passing it to the domain init was that nobody had written it in
 * this shape. Truncates at `cap`, like every other bounded writer here. */
size_t rolltui_undeliverable_reason_fn(void* ctx, const RolltuiChord* k, unsigned char protocol, char* out,
                                       size_t cap);

/* The English for why a chord cannot be delivered, into a caller buffer of at least
 * ROLLTUI_UNDELIVERABLE_REASON_MAX bytes — deliberately not duplicated here (see the header
 * comment above this section). Returns the length written. */
#define ROLLTUI_UNDELIVERABLE_REASON_MAX 128
typedef size_t (*RolltuiReasonFn)(void* ctx, const RolltuiChord* k, unsigned char protocol, char* out, size_t cap);

/* ADDS a file's rows to `b`, which the caller constructs first (`Bindings::Bindings()` seeds
 * the library's own actions before calling this, exactly as the original C++ loop started
 * from `Bindings b;`) — so an action the caller already declared is never re-added, and its
 * row, if the file has one, simply gains chords. `deliver` is the protocol every chord in the
 * file is checked against. `is_library`/`reason` are the two vocabulary questions above,
 * asked back through callbacks.
 *
 * Returns 0 only when the file is fundamentally unusable (not a JSON object, or no "bindings"
 * object) — `report->error` says which, and `b` is left exactly as it was passed in. A lesser
 * problem is reported and `b` still gains whatever the file was good for, matching the C++
 * original's "a file with problems still loads" contract. `report` is reset (as if freshly
 * zero-initialised) on every call, success or failure. */
int rolltui_bindings_load_json(RolltuiBindings* b, const char* text, size_t len, unsigned char deliver_protocol,
                               RolltuiScopeFn is_library, void* library_ctx, RolltuiReasonFn reason,
                               void* reason_ctx, RolltuiBindingsReport* report);

/* Serialises to TEXT: {"name", "bindings": {action: [chord, ...], ...}}, 2-space indented with
 * a trailing newline (matches `json::dump(v, 2) + "\n"`). REPLACES `*out`. Every row is
 * written, declared or not — the file is the whole domain (rule 1), and an undeclared row's
 * chords must round-trip (Bindings.hpp's kept-and-inert rule). */
void rolltui_bindings_dump_json(const RolltuiBindings* b, const char* name, size_t name_len, RolltuiStr* out);


/* ---- DECLARING, SUGGESTING, AND THE SHIPPED TABLE (Phase 17) ----------------------------
 * These four were the last of this module's BEHAVIOUR to live only in C++. Every primitive
 * they stand on was already here; what was missing was the composition — and a composition a
 * pure-C host has to re-derive is a duplicate implementation waiting to disagree.
 *
 * `RolltuiToolAction` is the row a MOUNTED TOOL brings: its name, its English, and the chord
 * it SUGGESTS. A tool states its keys in code because no bindings file can — the shipped file
 * belongs to every host, and a row in it for a tool most hosts never mount is a key they
 * advertise and cannot press. */
/* FORWARD, never an include: `rolltui_layout.h` includes THIS header, so including it back
 * would be a cycle in which whichever of the two a translation unit reached first saw the
 * other's types undefined. `rolltui_bindings_declare` only ever takes a POINTER to one, so a
 * forward declaration is all it needs. Repeating the typedef identically is legal in both C11
 * and C++, which is what lets the definition stay in the module that owns it. */
typedef struct RolltuiLayoutAction RolltuiLayoutAction;

typedef struct RolltuiToolAction {
  const char* name;
  const char* description;
  const char* chord; /* "ctrl+q" — a SUGGESTION, never an override. NULL or "" for none. */
} RolltuiToolAction;

/* Is `scope` one the LIBRARY defines? True for exactly the scopes of the closed table above
 * (`input`, `transcript`, `menu`, `edit`, `stack`) — a scope is the library's because the
 * library DEFINES it, never because the library happens to ship the tool. Matches
 * `RolltuiScopeFn`, so it can be passed straight to `rolltui_bindings_undeclare_others`. */
int rolltui_bindings_library_scope(void* ctx, const char* scope, size_t len);

/* AUTHORITATIVE over every non-library scope: after this call the declared non-library
 * actions are EXACTLY `declared` + `tools`, so loading another screen makes the last one's
 * inert. Merely ADDING would leave a key working because of a layout no longer running.
 *
 * THE ORDER INSIDE IS LOAD-BEARING AND IS WHY THESE ARE ONE CALL: the suggestions go in
 * FIRST, because declaring an action creates an empty row for it, and a suggestion made
 * afterwards would see that row and decline every time — a tool whose keys are all silently
 * unbound, which is exactly what the first cut of this did. */
void rolltui_bindings_declare(RolltuiBindings* b, const RolltuiLayoutAction* declared, size_t declared_n,
                              const RolltuiToolAction* tools, size_t tools_n);

/* THE SHIPPED DEFAULT TABLE: the embedded `default` bindings file, parsed and validated once
 * and cached for the life of the process (released by `rolltui_shutdown`). BORROWED — never
 * freed, never mutated by the caller; clone it to edit.
 *
 * IT ABORTS ON TWO BUILD MISTAKES, deliberately, because both are the LIBRARY's error and not
 * a user's, and both would otherwise ship:
 *   1. the file does not load cleanly — checked against the WEAKEST key protocol (Legacy) and
 *      not against whatever this terminal turned out to be, because the shipped file belongs
 *      to every host on every terminal. A chord that only works on kitty is fine in a user's
 *      own file and a build mistake in this one;
 *   2. it binds an action no shipped layout declares — a key every host advertises and cannot
 *      press. A mounted tool's chords come from the tool, so a tool row here stops the build. */
/* A table SEEDED with the library's own: the Enter rule set, and all 59 closed actions
 * declared. This is what every host actually starts from, and what `rolltui_bindings_load_json`
 * means by "the caller constructs first" — its contract is to ADD a file's rows to a table that
 * already knows the library's vocabulary, so a file naming `input.submit` is recognised rather
 * than reported as an unknown action. Loading the shipped file into a bare
 * `rolltui_bindings_new()` reports all 59 as unknown; that is not a defect in either call, it
 * is the seeding this one names.
 *
 * `rolltui_bindings_new` stays the EMPTY one, because a test that wants to watch rows appear
 * needs a table with nothing in it. */
RolltuiBindings* rolltui_bindings_new_seeded(void);

const RolltuiBindings* rolltui_bindings_default(void);


/* ========================================================================================
 * style — RolltuiStyle and the role list
 * ======================================================================================== */
#define ROLLTUI_ROLE_LIST(X) \
  X(text, TEXT) \
  X(text_muted, TEXT_MUTED) \
  X(background, BACKGROUND) \
  X(panel_background, PANEL_BACKGROUND) \
  X(border, BORDER) \
  X(border_active, BORDER_ACTIVE) \
  X(title, TITLE) \
  X(label, LABEL) \
  X(value, VALUE) \
  X(accent_1, ACCENT_1) \
  X(accent_2, ACCENT_2) \
  X(accent_3, ACCENT_3) \
  X(accent_4, ACCENT_4) \
  X(prompt, PROMPT) \
  X(note, NOTE) \
  X(warning, WARNING) \
  X(error, ERROR) \
  X(md_heading, MD_HEADING) \
  X(md_emphasis, MD_EMPHASIS) \
  X(md_strong, MD_STRONG) \
  X(md_code_inline, MD_CODE_INLINE) \
  X(md_code_block, MD_CODE_BLOCK) \
  X(md_code_label, MD_CODE_LABEL) \
  X(md_link, MD_LINK) \
  X(md_link_url, MD_LINK_URL) \
  X(md_quote, MD_QUOTE) \
  X(md_list_marker, MD_LIST_MARKER) \
  X(md_table_border, MD_TABLE_BORDER) \
  X(md_table_header, MD_TABLE_HEADER) \
  X(md_rule, MD_RULE) \
  X(md_strikethrough, MD_STRIKETHROUGH) \
  X(diff_added, DIFF_ADDED) \
  X(diff_removed, DIFF_REMOVED) \
  X(diff_context, DIFF_CONTEXT) \
  X(diff_added_word, DIFF_ADDED_WORD) \
  X(diff_removed_word, DIFF_REMOVED_WORD) \
  X(input_text, INPUT_TEXT) \
  X(input_cursor, INPUT_CURSOR) \
  X(input_placeholder, INPUT_PLACEHOLDER) \
  X(scroll_marker, SCROLL_MARKER) \
  X(selection, SELECTION) \
  X(overlay, OVERLAY) \
  X(menu_item, MENU_ITEM) \
  X(menu_selected, MENU_SELECTED) \
  X(menu_breadcrumb, MENU_BREADCRUMB) \
  X(menu_shortcut, MENU_SHORTCUT) \
  X(find_match, FIND_MATCH) \
  X(find_current, FIND_CURRENT) \
  X(scrollbar, SCROLLBAR) \

typedef enum RolltuiRole {
#define ROLLTUI_ROLE_ENUM_(lower, UPPER) ROLLTUI_ROLE_##UPPER,
  ROLLTUI_ROLE_LIST(ROLLTUI_ROLE_ENUM_)
#undef ROLLTUI_ROLE_ENUM_
  ROLLTUI_ROLE_COUNT
} RolltuiRole;

/* The three that cross as struct-field DEFAULTS keep their old names, because a default is
 * the one case the "a renderer is handed the byte" rule cannot cover: there is no call at
 * which to hand one in. They are now ALIASES of the enum rather than hand-written numbers,
 * so the comment that used to say `Role::text` is the definition instead of a promise. */
#define ROLLTUI_ROLE_DEFAULT_TEXT ROLLTUI_ROLE_TEXT
#define ROLLTUI_ROLE_DEFAULT_BACKGROUND ROLLTUI_ROLE_BACKGROUND
#define ROLLTUI_ROLE_DEFAULT_PROMPT ROLLTUI_ROLE_PROMPT

/* The role's name, as the themes and layout files spell it ("md_code_block"). BORROWS a
 * static literal, valid for the life of the process; `*len` may be NULL. Returns "" for an
 * out-of-range value rather than reading past the table. */
const char* rolltui_role_name(unsigned char role, size_t* len);

/* The role of that name, or -1 when there is none. This is what a theme LOADER needs and had
 * no way to ask for in C — `rolltui_theme_load` was handed a vocabulary by its caller for
 * exactly this reason, and can now be handed the library's own. */
int rolltui_role_from_name(const char* name, size_t len);

typedef struct RolltuiStyleColor {
#ifdef __cplusplus
  enum class Kind : unsigned char { None = 0, Indexed = 1, Rgb = 2 };
  Kind kind = Kind::None;
#else
  unsigned char kind; /* 0 none, 1 indexed, 2 rgb */
#endif
  unsigned char index ROLLTUI_DEFAULT(0);              /* Indexed: 0-255 */
  unsigned char r ROLLTUI_DEFAULT(0), g ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0); /* Rgb */

#ifdef __cplusplus
  static constexpr RolltuiStyleColor none() { return {}; }
  static constexpr RolltuiStyleColor indexed(unsigned char i) {
    RolltuiStyleColor c;
    c.kind = Kind::Indexed;
    c.index = i;
    return c;
  }
  static constexpr RolltuiStyleColor rgb(unsigned char red, unsigned char green, unsigned char blue) {
    RolltuiStyleColor c;
    c.kind = Kind::Rgb;
    c.r = red;
    c.g = green;
    c.b = blue;
    return c;
  }
  constexpr bool operator==(const RolltuiStyleColor&) const = default;
#endif
} RolltuiStyleColor;
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiStyleColor) == 5, "RolltuiStyleColor must be five bytes in both languages");

typedef struct RolltuiStyle {
  RolltuiStyleColor fg, bg;
  unsigned char bold ROLLTUI_DEFAULT(0), italic ROLLTUI_DEFAULT(0), underline ROLLTUI_DEFAULT(0),
      dim ROLLTUI_DEFAULT(0), reverse ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  constexpr bool operator==(const RolltuiStyle&) const = default;
#endif
} RolltuiStyle;
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiStyle) == 15, "RolltuiStyle must be fifteen bytes with no padding in either language");


/* ========================================================================================
 * document — the transcript document a host appends to
 * ======================================================================================== */
#ifdef __cplusplus
namespace rolltui {
// Declared, not defined: the vocabularies live in `rolltui/Style.hpp` and
// `rolltui/Effects.hpp`, and this file names no role and no state (the m2 rule at
// `rolltui_diff.h`). An opaque enum declaration is a complete type because the underlying
// type is fixed, which is all a member needs.
enum class Role : unsigned char;
enum class EffectState : unsigned char;

}  // namespace rolltui
#endif
typedef struct RolltuiDocEntry {
  RolltuiStr id; /* stable identity across frames */
  unsigned long long version ROLLTUI_DEFAULT(0);
  RolltuiStr text;                           /* markdown source, or verbatim text */
  unsigned char markdown ROLLTUI_DEFAULT(1); /* 0: rendered as plain wrapped lines */
#ifdef __cplusplus
  rolltui::Role role = static_cast<rolltui::Role>(ROLLTUI_ROLE_DEFAULT_TEXT);
#else
  unsigned char role;
#endif
  RolltuiStr prefix; /* drawn before the first line, in `prefix_role` */
#ifdef __cplusplus
  rolltui::Role prefix_role = static_cast<rolltui::Role>(ROLLTUI_ROLE_DEFAULT_PROMPT);
#else
  unsigned char prefix_role;
#endif
  unsigned char foldable ROLLTUI_DEFAULT(0);
  RolltuiStr summary;                      /* the one-line summary a foldable entry shows */
  unsigned char folded ROLLTUI_DEFAULT(1); /* initial fold state of a foldable entry */
#ifdef __cplusplus
  rolltui::EffectState state = static_cast<rolltui::EffectState>(0); /* None */
#else
  unsigned char state;
#endif
  double progress ROLLTUI_DEFAULT(0); /* Progress: 0..1 */
  unsigned long long state_since_ms ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiDocEntry();
  // COPY IS DELETED (Phase 19 m2): the explicit spelling is `clone()`, which is
  // `rolltui_doc_entry_copy`. MOVE and the destructor stay — the destructor calls the named
  // `rolltui_doc_entry_release` and hides nothing.
  RolltuiDocEntry(const RolltuiDocEntry&) = delete;
  RolltuiDocEntry(RolltuiDocEntry&& o) noexcept;
  RolltuiDocEntry& operator=(const RolltuiDocEntry&) = delete;
  RolltuiDocEntry& operator=(RolltuiDocEntry&& o) noexcept;
  ~RolltuiDocEntry();
  RolltuiDocEntry clone() const;
#endif
} RolltuiDocEntry;

void rolltui_doc_entry_init(RolltuiDocEntry* e);
void rolltui_doc_entry_release(RolltuiDocEntry* e);
void rolltui_doc_entry_copy(RolltuiDocEntry* to, const RolltuiDocEntry* from);

/* The entries, OWNED and individually allocated so an append never moves one. Laid out as
 * `RolltuiPtrVec` by construction, so the append mechanics are that one's. */
typedef struct RolltuiDocument {
  RolltuiDocEntry** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  struct iterator {
    RolltuiDocEntry** p;
    RolltuiDocEntry& operator*() const { return **p; }
    RolltuiDocEntry* operator->() const { return *p; }
    iterator& operator++() {
      ++p;
      return *this;
    }
    bool operator==(const iterator& o) const { return p == o.p; }
  };
  struct const_iterator {
    RolltuiDocEntry* const* p;
    const RolltuiDocEntry& operator*() const { return **p; }
    const RolltuiDocEntry* operator->() const { return *p; }
    const_iterator& operator++() {
      ++p;
      return *this;
    }
    bool operator==(const const_iterator& o) const { return p == o.p; }
  };

  RolltuiDocument() = default;
  RolltuiDocument(const RolltuiDocument&) = delete;  /* clone() is the spelling (Phase 19 m2) */
  RolltuiDocument(RolltuiDocument&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiDocument& operator=(const RolltuiDocument&) = delete;
  RolltuiDocument& operator=(RolltuiDocument&& o) noexcept;
  ~RolltuiDocument();
  RolltuiDocument clone() const;

  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  RolltuiDocEntry& operator[](std::size_t i) { return *v[i]; }
  const RolltuiDocEntry& operator[](std::size_t i) const { return *v[i]; }
  RolltuiDocEntry& back() { return *v[n - 1]; }
  const RolltuiDocEntry& back() const { return *v[n - 1]; }
  iterator begin() { return {v}; }
  iterator end() { return {v + n}; }
  const_iterator begin() const { return {v}; }
  const_iterator end() const { return {v + n}; }
  void push_back(RolltuiDocEntry&& e);
  void push_back(const RolltuiDocEntry& e);
  void clear();
  /* Trims or grows, KEEPING the storage past the end — `std::vector<DocEntry>::resize`
   * destroys, and a transcript that trims and refills wants the entries back. */
  void resize(std::size_t k);
#endif
} RolltuiDocument;

size_t rolltui_document_count(const RolltuiDocument* d);
RolltuiDocEntry* rolltui_document_at(const RolltuiDocument* d, size_t i);
/* Appends an EMPTY entry and returns it — the C's `emplace_back`. */
RolltuiDocEntry* rolltui_document_add(RolltuiDocument* d);
void rolltui_document_clear(RolltuiDocument* d);   /* releases every entry, keeps the array */
void rolltui_document_release(RolltuiDocument* d); /* …and the array */
void rolltui_document_copy(RolltuiDocument* to, const RolltuiDocument* from);

#ifdef __cplusplus
/* ---- the C++ special members of the structs above (Phase 17 m3) ---------------------------
 * Each one is a CALLER of a C function declared above it, so "release this subtree" has one
 * implementation and a destructor reaches it rather than being a second mechanism.
 *
 * They were out-of-line in `rolltui/DocumentCpp.cpp` until the C++ binding was deleted. They are not
 * part of that binding — they are what makes "the C++ type IS the C struct" true (Phase 14's
 * one-definition rule), so they had to keep a home; `inline`, beside the declarations they
 * implement, is that home and removes the last C++ translation unit from the library. */
inline RolltuiDocEntry::RolltuiDocEntry() = default;
inline RolltuiDocEntry::~RolltuiDocEntry() = default;

inline RolltuiDocEntry::RolltuiDocEntry(RolltuiDocEntry&& o) noexcept
    : id(std::move(o.id)),
      version(o.version),
      text(std::move(o.text)),
      markdown(o.markdown),
      role(o.role),
      prefix(std::move(o.prefix)),
      prefix_role(o.prefix_role),
      foldable(o.foldable),
      summary(std::move(o.summary)),
      folded(o.folded),
      state(o.state),
      progress(o.progress),
      state_since_ms(o.state_since_ms) {}
inline RolltuiDocEntry RolltuiDocEntry::clone() const {
  RolltuiDocEntry out;
  rolltui_doc_entry_copy(&out, this);
  return out;
}

inline RolltuiDocEntry& RolltuiDocEntry::operator=(RolltuiDocEntry&& o) noexcept {
  if (this != &o) {
    id = std::move(o.id);
    version = o.version;
    text = std::move(o.text);
    markdown = o.markdown;
    role = o.role;
    prefix = std::move(o.prefix);
    prefix_role = o.prefix_role;
    foldable = o.foldable;
    summary = std::move(o.summary);
    folded = o.folded;
    state = o.state;
    progress = o.progress;
    state_since_ms = o.state_since_ms;
  }
  return *this;
}

inline RolltuiDocument RolltuiDocument::clone() const {
  RolltuiDocument out;
  rolltui_document_copy(&out, this);
  return out;
}

inline RolltuiDocument::~RolltuiDocument() { rolltui_document_release(this); }

inline RolltuiDocument& RolltuiDocument::operator=(RolltuiDocument&& o) noexcept {
  if (this != &o) {
    rolltui_document_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}

inline void RolltuiDocument::push_back(RolltuiDocEntry&& e) { *rolltui_document_add(this) = std::move(e); }

inline void RolltuiDocument::push_back(const RolltuiDocEntry& e) {
  rolltui_doc_entry_copy(rolltui_document_add(this), &e);
}

inline void RolltuiDocument::clear() { rolltui_document_clear(this); }

inline void RolltuiDocument::resize(std::size_t k) {
  while (n > k) rolltui_doc_entry_release(v[--n]);
  while (n < k) rolltui_document_add(this);
}

#endif

/* ========================================================================================
 * geom — RolltuiRect, the vocabulary every layout speaks
 * ======================================================================================== */
/* The intersection of two rectangles, written into `out` as {x, y, w, h}. An empty result
 * is {x0, y0, 0, 0} where (x0, y0) is the clamped origin — NOT {0,0,0,0}, because callers
 * position things relative to it. */
void rolltui_rect_intersect(int ax, int ay, int aw, int ah,
                            int bx, int by, int bw, int bh,
                            int out[4]);

/* ---- a rectangle, defined ONCE and compiled by both languages (Phase 15 m5) ------------ */
/* `rolltui::Rect` IS this struct. It moved here the moment the C had to HOLD one rather
 * than take four ints: a layout node's outer and inner boxes are the split's whole output,
 * and passing them as sixteen loose integers would have been the "layout-compatible by
 * fiat" this project keeps being burned by. The four ints in / four out below stay, because
 * they are what the C++ side's `intersect` is implemented in terms of and what the flag
 * still chooses between. */
typedef struct RolltuiRect {
  int x ROLLTUI_DEFAULT(0), y ROLLTUI_DEFAULT(0), w ROLLTUI_DEFAULT(0), h ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  RolltuiRect intersect(const RolltuiRect& o) const {  /* still through the seam */
    int r[4];
    rolltui_rect_intersect(x, y, w, h, o.x, o.y, o.w, o.h, r);
    return RolltuiRect{r[0], r[1], r[2], r[3]};
  }
  bool empty() const { return w <= 0 || h <= 0; }
  bool operator==(const RolltuiRect&) const = default;
#endif
} RolltuiRect;
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiRect) == 16, "a Rect must be four ints in both languages");


/* ========================================================================================
 * screen — the cell grid
 * ======================================================================================== */
/* One cell: a grapheme cluster, its style, its width and its hyperlink id.
 *
 * A GRAPHEME CLUSTER HAS NO MAXIMUM LENGTH, so the long case is handled rather than assumed
 * away: ten bytes covers ASCII, accented Latin, CJK, an emoji with a variation selector, a
 * flag and an emoji with a skin-tone modifier, and anything longer — a family ZWJ sequence
 * is 25+ bytes, and a user can paste one — SPILLS into a table the frame owns, with its
 * index kept where the bytes would have been (`len` reads ROLLTUI_CELL_SPILLED). That is
 * exactly the shape `link` already has, which is why it is the shape used: one mechanism,
 * twice. Spilling is Phase 13's "an allocation may happen, but it has a NAME" case. */
#define ROLLTUI_CELL_INLINE_GLYPH 10
#define ROLLTUI_CELL_SPILLED 0xFF
typedef struct RolltuiCell {
  unsigned int link ROLLTUI_DEFAULT(0); /* 0: none; else an id from rolltui_frame_link_id */
  RolltuiStyle style;
  char bytes[ROLLTUI_CELL_INLINE_GLYPH] ROLLTUI_DEFAULT({' '}); /* the cluster, or its spill index */
  unsigned char len ROLLTUI_DEFAULT(1);   /* bytes in `bytes`; 0 on a continuation cell */
  unsigned char width ROLLTUI_DEFAULT(1); /* 1 or 2; 0 on a continuation cell */
  unsigned char continuation ROLLTUI_DEFAULT(0); /* the right half of a 2-cell glyph */

#ifdef __cplusplus
  static constexpr unsigned char kInlineGlyph = ROLLTUI_CELL_INLINE_GLYPH;
  static constexpr unsigned char kSpilled = ROLLTUI_CELL_SPILLED;

  bool spilled() const { return len == kSpilled; }
  bool operator==(const RolltuiCell&) const = default;
#endif
} RolltuiCell;
/* THE CELL HAS NO PADDING, and that is asserted rather than hoped: `rolltui_frame_equal`
 * compares cells with `memcmp`, which is only right when every byte of the struct is a
 * byte somebody wrote. A field added without thought would break this line before it could
 * make equality read uninitialised padding and call two identical frames different. */
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiCell) == 4 + 15 + ROLLTUI_CELL_INLINE_GLYPH + 3,
                      "RolltuiCell has padding; memcmp equality would compare bytes nobody wrote");

typedef struct RolltuiFrame RolltuiFrame;

/* ---- lifetime -------------------------------------------------------------------- */
/* OWNED by the caller. `new` never returns NULL: an allocation failure aborts inside
 * rolltui::mem, because a half-built frame is worse than a clean death. */
RolltuiFrame* rolltui_frame_new(int w, int h, RolltuiStyle fill);
void rolltui_frame_free(RolltuiFrame* f);
/* Reuses every buffer it can (Phase 13 m5): the cells, the link table's strings and the
 * spill table's. A steady frame allocates nothing through here. */

/* ---- geometry and cells ----------------------------------------------------------- */
int rolltui_frame_width(const RolltuiFrame* f);
int rolltui_frame_height(const RolltuiFrame* f);
/* A COPY of the cell, into `out`, WHICH THE CALLER OWNS. Out of bounds writes a zeroed cell
 * with width 0.
 *
 * WHY A CALLER'S BUFFER AND NOT A RETURN VALUE, since a POD may cross this boundary (m1's
 * rule, revised for m2): a struct PARAMETER by value has one answer every ABI agrees on for
 * a trivially-copyable type, and a struct RETURN does not — Clang says so itself, with
 * `-Wreturn-type-c-linkage` on an `extern "C"` function returning a class that is not a
 * C++98 POD, which `RolltuiCell` stopped being the moment it gained the methods and default
 * initializers that make one definition possible. Suppressing that warning would be
 * asserting an ABI the compiler declines to promise, which is this project's exact failure
 * shape. A caller's buffer was already the rule (rolltui_geom.h), and it costs one line. */



/* ---- marks (the widget's whole vocabulary for motion) ------------------------------ */
/* `state` is rolltui::EffectState as an int; the C side stores it and never interprets it,
 * which is what keeps the effects vocabulary in one place (Effects.hpp) rather than two.
 * The one exception is that 0 means None and is not recorded, which is the same rule the
 * C++ had — "is anything marked" and "does anything move" stay the same question. */
void rolltui_frame_mark(RolltuiFrame* f, int x, int y, int cells, int state,
                        unsigned long long since_ms, double fraction);
size_t rolltui_frame_mark_count(const RolltuiFrame* f);

/* ---- cursor ------------------------------------------------------------------------ */
void rolltui_frame_set_cursor(RolltuiFrame* f, int x, int y, int visible);



/* ========================================================================================
 * frame_ops — drawing into the cell grid
 * ======================================================================================== */
typedef struct RolltuiDrawScratch RolltuiDrawScratch;
RolltuiDrawScratch* rolltui_draw_scratch_new(void);
void rolltui_draw_scratch_free(RolltuiDrawScratch* s); /* a no-op on NULL */

/* Writes `utf8` at (x, y), cluster by cluster, stopping at `max_cells`, at the frame's right
 * edge, or before a wide glyph that would be cut in half. Returns the cells used. */
int rolltui_frame_put_text(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                           RolltuiStyle style, int max_cells, int ambiguous_wide, unsigned int link);

/* Fills `r` (clipped) with a repeated grapheme — a space when `glyph` is NULL or has no
 * width. */
void rolltui_frame_fill(RolltuiFrame* f, RolltuiDrawScratch* s, RolltuiRect r, RolltuiStyle style,
                        const char* glyph, size_t glyph_len);

/* Amends every cell's style in `r` (clipped): a set colour replaces, an attribute bit is
 * OR'd in. Needs no scratch — it reads and writes styles and never looks at text. */
void rolltui_frame_tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style);


/* ========================================================================================
 * input — the input widget: a host sets, selects, undoes and reads
 * ======================================================================================== */
#ifdef __cplusplus
namespace rolltui {
enum class Role : unsigned char;  // declared, not defined: this file names no role

}  // namespace rolltui
#endif
/* ---- what a handle() call answers ------------------------------------------------------- */
#define ROLLTUI_INPUT_IGNORED 0
#define ROLLTUI_INPUT_HANDLED 1
#define ROLLTUI_INPUT_SUBMIT 2
#define ROLLTUI_INPUT_EOF 3

/* ---- options ---------------------------------------------------------------------------- */
/* `rolltui::InputOptions` IS this struct (one definition). The two strings are OWNED, which
 * the C++ said with `std::string` and never had to think about; here they are `RolltuiStr`
 * and the copy is a function with a name. */
typedef struct RolltuiInputOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int tab_width ROLLTUI_DEFAULT(4);
  int inset ROLLTUI_DEFAULT(0); /* columns kept clear on each side of the area */
  RolltuiStr prompt;            /* drawn before the first row, in prompt_role */
#ifdef __cplusplus
  rolltui::Role prompt_role = static_cast<rolltui::Role>(ROLLTUI_ROLE_DEFAULT_PROMPT);
#else
  unsigned char prompt_role;
#endif
  RolltuiStr placeholder; /* drawn after the prompt while the text is empty */
  unsigned long long multi_click_ms ROLLTUI_DEFAULT(400);
  size_t history_limit ROLLTUI_DEFAULT(1000);
  unsigned char single_line ROLLTUI_DEFAULT(0); /* a newline is dropped — a menu field */

#ifdef __cplusplus
  RolltuiInputOptions();
  bool operator==(const RolltuiInputOptions& o) const;
#endif
} RolltuiInputOptions;

/* The defaults, for a C caller — `= {0}` would give a zero tab width and no prompt role,
 * which is Phase 14's design lens exactly. */
void rolltui_input_options_release(RolltuiInputOptions* o);
void rolltui_input_options_copy(RolltuiInputOptions* to, const RolltuiInputOptions* from);
int rolltui_input_options_equal(const RolltuiInputOptions* a, const RolltuiInputOptions* b);

/* ---- the selection ------------------------------------------------------------------------ */
/* `rolltui::InputSelection` IS this struct. */
typedef struct RolltuiInputSelection {
  size_t anchor ROLLTUI_DEFAULT(0), head ROLLTUI_DEFAULT(0);
  unsigned char active ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  size_t begin() const { return anchor < head ? anchor : head; }
  size_t end() const { return anchor < head ? head : anchor; }
  bool empty() const { return !active || anchor == head; }
  bool operator==(const RolltuiInputSelection&) const = default;
#endif
} RolltuiInputSelection;

typedef struct RolltuiInput RolltuiInput;

/* THE HOST'S CLIPBOARD, as a function pointer and a context rather than a `std::function`.
 * Set once; NULL turns it off. The text is a BORROW for the call. */
typedef void (*RolltuiCopyFn)(void* ctx, const char* text, size_t len);
void rolltui_input_set_copy(RolltuiInput* in, RolltuiCopyFn fn, void* ctx);

/* ---- content -------------------------------------------------------------------------------- */
/* A BORROW, valid until the text next changes. */
const char* rolltui_input_text(const RolltuiInput* in, size_t* len);
void rolltui_input_clear(RolltuiInput* in);
/* The selected bytes, a BORROW into the text; `*len` 0 when there is no selection. */




/* THE THIRTY ACTION NAMES, in command order, handed over by the shim. This file knows what
 * each command DOES and none of the words; `rolltui/Input.hpp` lists them and
 * `library_actions()` in `Bindings.cpp` is where they are written down. */
typedef struct RolltuiInputActions {
  const char* submit;
  const char* newline;
  const char* backspace;
  const char* del;
  const char* kill_word_backward;
  const char* kill_word_forward;
  const char* kill_to_line_start;
  const char* kill_to_line_end;
  const char* left;
  const char* right;
  const char* word_left;
  const char* word_right;
  const char* line_start;
  const char* line_end;
  const char* up;
  const char* down;
  const char* select_left;
  const char* select_right;
  const char* select_word_left;
  const char* select_word_right;
  const char* select_line_start;
  const char* select_line_end;
  const char* select_up;
  const char* select_down;
  const char* select_all;
  const char* clear_selection;
  const char* copy;
  const char* eof;
  const char* undo;
  const char* redo;
} RolltuiInputActions;

/* The LIBRARY'S OWN thirty, so a consumer need not spell them to call `handle` (Phase 17 m2a).
 * BORROWS static storage. `rolltui_library_actions.c` expands one list into this and three
 * siblings; a host with different words still passes its own struct. */
const RolltuiInputActions* rolltui_input_default_actions(void);


/* ---- layout and drawing -------------------------------------------------------------------------- */
void rolltui_input_set_options(RolltuiInput* in, const RolltuiInputOptions* o);
const RolltuiInputOptions* rolltui_input_options(const RolltuiInput* in);
/* Where a position is drawn: a text row (before scrolling) and a column from the area's left
 * edge, the prompt / indent included. */

/* THE FOUR ROLES A DRAW NEEDS, handed in as bytes. `prompt` is the OPTIONS' role, which is
 * why it is not in here. */
typedef struct RolltuiInputRoles {
  unsigned char text;
  unsigned char selection;
  unsigned char placeholder;
} RolltuiInputRoles;


#ifdef __cplusplus
/* `RolltuiInputOptions`' one special member (Phase 17 m3): the default prompt, which is a
 * VALUE the struct must start with and not something a caller should have to know. It was
 * out-of-line in `rolltui/Input.cpp` for the reason `RolltuiActionList`'s were — an inline body
 * inside the struct cannot see `rolltui_str_set` yet — and, like those, it is not part of the
 * deleted binding but part of what makes the C++ type BE the C struct. */
inline RolltuiInputOptions::RolltuiInputOptions() { rolltui_str_set(&prompt, "> ", 2); }

#endif

/* ========================================================================================
 * md_lines — the transcript's line store
 * ======================================================================================== */
typedef struct RolltuiMdLines RolltuiMdLines;

/* ---- working memory the store lends its filler ------------------------------------------
 *
 * CLAUDE.md's third strategy covers WORKING memory, not only results: a function that needs
 * somewhere to decode into takes a handle the caller owns. Both implementations of the
 * renderer cluster text constantly, and this is the buffer they do it in — one per store,
 * so a renderer holds no per-thread state of its own. */
typedef struct RolltuiUnicodeScratch RolltuiUnicodeScratch;


/* ========================================================================================
 * markdown — the markdown parser over md4c; the transcript renders through md_lines, and no host calls it
 * ======================================================================================== */
/* One verbatim line of a code block, as a borrow. */
typedef struct RolltuiMdCodeLine {
  const char* p;
  size_t n;
} RolltuiMdCodeLine;

/* Where a highlighter puts one span. Supplied by the renderer; valid for the call only. */
typedef void (*RolltuiMdSpanSink)(void* sink, size_t begin, size_t end, unsigned char role);

/* THE SYNTAX-HIGHLIGHTING SEAM (plan/phase-12.md m2), as a function pointer.
 *
 * Emits the spans of `lines[index]` through `sink`, in any order and any number. It emits
 * DATA, never a painter — the renderer alone decides how those bytes wrap and land in the
 * cell grid, and a span that overlaps a prior one, runs backwards or exceeds the line is
 * clamped and NAMED in the store's report rather than corrupting a frame. Called ONCE per
 * code line of every Code block, and never for an HTML block. */
typedef void (*RolltuiMdHighlightFn)(void* ctx, const char* lang, size_t lang_n,
                                     const RolltuiMdCodeLine* lines, size_t line_count, size_t index,
                                     RolltuiMdSpanSink emit, void* sink);


/* ========================================================================================
 * menu_tree — the menu item tree a host builds and walks
 * ======================================================================================== */
#ifdef __cplusplus
namespace rolltui {
enum class InputType : unsigned char { Text, Int, Float, Color, Size, Dim, Name };

}  // namespace rolltui
#endif
#define ROLLTUI_INPUT_TYPE_TEXT 0
#define ROLLTUI_INPUT_TYPE_INT 1
#define ROLLTUI_INPUT_TYPE_FLOAT 2
#define ROLLTUI_INPUT_TYPE_COLOR 3
#define ROLLTUI_INPUT_TYPE_SIZE 4
#define ROLLTUI_INPUT_TYPE_DIM 5
#define ROLLTUI_INPUT_TYPE_NAME 6
#define ROLLTUI_INPUT_TYPE_COUNT 7

/* What an Input item accepts. `rolltui::InputSpec` IS this struct. */
typedef struct RolltuiInputSpec {
#ifdef __cplusplus
  rolltui::InputType type = rolltui::InputType::Text;
#else
  unsigned char type;
#endif
  double min ROLLTUI_DEFAULT(-1e15), max ROLLTUI_DEFAULT(1e15); /* Int / Float, inclusive */
  double step ROLLTUI_DEFAULT(1);                               /* Up / Down while editing a number */
  int precision ROLLTUI_DEFAULT(-1);   /* Float: most digits after the point; -1 = any */
  size_t max_len ROLLTUI_DEFAULT(0);   /* Text: 0 = no cap; Name: the type's own 64 */
  size_t min_len ROLLTUI_DEFAULT(0);   /* Text: checked at commit */
  unsigned char optional ROLLTUI_DEFAULT(0); /* empty commits as "" instead of being refused */
  RolltuiStr validator;                /* Text: a host-registered check by name, at commit */
  RolltuiStr hint;                     /* shown beside the field; input_hint(spec) when empty */

#ifdef __cplusplus
  bool operator==(const RolltuiInputSpec& o) const;
  RolltuiInputSpec clone() const;  /* `rolltui_input_spec_copy`, spelled (Phase 19 m2) */
#endif
} RolltuiInputSpec;

void rolltui_input_spec_init(RolltuiInputSpec* s);
void rolltui_input_spec_release(RolltuiInputSpec* s);
void rolltui_input_spec_copy(RolltuiInputSpec* to, const RolltuiInputSpec* from);
int rolltui_input_spec_equal(const RolltuiInputSpec* a, const RolltuiInputSpec* b);

struct RolltuiMenuItem;

/* An item's OWNED children (a Submenu's items, a Choice's options). Pointers, for the reason
 * `RolltuiNodeList` holds pointers: a child's address never moves. */
typedef struct RolltuiMenuItemList {
  struct RolltuiMenuItem** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  struct iterator {
    RolltuiMenuItem** p;
    RolltuiMenuItem& operator*() const { return **p; }
    RolltuiMenuItem* operator->() const { return *p; }
    iterator& operator++() {
      ++p;
      return *this;
    }
    iterator operator+(std::ptrdiff_t d) const { return {p + d}; }
    std::ptrdiff_t operator-(const iterator& o) const { return p - o.p; }
    bool operator==(const iterator& o) const { return p == o.p; }
  };
  struct const_iterator {
    RolltuiMenuItem* const* p;
    const_iterator() : p(nullptr) {}
    const_iterator(RolltuiMenuItem* const* q) : p(q) {}  // NOLINT(google-explicit-constructor)
    const_iterator(iterator i) : p(i.p) {}               // NOLINT(google-explicit-constructor)
    const RolltuiMenuItem& operator*() const { return **p; }
    const RolltuiMenuItem* operator->() const { return *p; }
    const_iterator& operator++() {
      ++p;
      return *this;
    }
    const_iterator operator+(std::ptrdiff_t d) const { return {p + d}; }
    std::ptrdiff_t operator-(const const_iterator& o) const { return p - o.p; }
    bool operator==(const const_iterator& o) const { return p == o.p; }
  };
  using value_type = RolltuiMenuItem;

  RolltuiMenuItemList() = default;
  RolltuiMenuItemList(const RolltuiMenuItemList&) = delete;  /* Phase 19 m2: `rolltui_menu_list_copy`, spelled */
  RolltuiMenuItemList(RolltuiMenuItemList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiMenuItemList& operator=(const RolltuiMenuItemList&) = delete;
  RolltuiMenuItemList& operator=(RolltuiMenuItemList&& o) noexcept;
  ~RolltuiMenuItemList();

  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  RolltuiMenuItem& operator[](std::size_t i) { return *v[i]; }
  const RolltuiMenuItem& operator[](std::size_t i) const { return *v[i]; }
  RolltuiMenuItem& front() { return *v[0]; }
  const RolltuiMenuItem& front() const { return *v[0]; }
  RolltuiMenuItem& back() { return *v[n - 1]; }
  const RolltuiMenuItem& back() const { return *v[n - 1]; }
  iterator begin() { return {v}; }
  iterator end() { return {v + n}; }
  const_iterator begin() const { return {v}; }
  const_iterator end() const { return {v + n}; }
  void push_back(RolltuiMenuItem&& c);
  void clear();
  bool operator==(const RolltuiMenuItemList& o) const;
#endif
} RolltuiMenuItemList;

#define ROLLTUI_MENU_ACTION 0
#define ROLLTUI_MENU_SUBMENU 1
#define ROLLTUI_MENU_TOGGLE 2
#define ROLLTUI_MENU_CHOICE 3
#define ROLLTUI_MENU_INPUT 4

typedef struct RolltuiMenuItem {
#ifdef __cplusplus
  enum class Kind : unsigned char { Action = 0, Submenu, Toggle, Choice, Input };
  Kind kind = Kind::Action;
#else
  unsigned char kind;
#endif
  RolltuiStr id;          /* the action id the host binds; an option's value id */
  RolltuiStr label;
  RolltuiStr action_name; /* the BINDINGS action this item does the same thing as */
  RolltuiStr shortcut;    /* display only; with `action_name` set it is the live chords */
  unsigned char enabled ROLLTUI_DEFAULT(1);
  unsigned char checked ROLLTUI_DEFAULT(0); /* Toggle */
  RolltuiStr value;       /* Choice: the current option id; Input: the COMMITTED text */
  RolltuiInputSpec spec;  /* Input: the type and its constraints */
  RolltuiMenuItemList children; /* Submenu: items; Choice: options */

#ifdef __cplusplus
  RolltuiMenuItem();
  bool operator==(const RolltuiMenuItem& o) const;
  RolltuiMenuItem clone() const;
  // The builders every host wrote items with — kept, in rolltui's own vocabulary (Phase 19 m2):
  // C strings in, `rolltui_menu_item_set` underneath. The std::string forms are gone; a host
  // with a std::string passes `.c_str()`.
  static RolltuiMenuItem action(const char* id, const char* label, const char* shortcut = "");
  static RolltuiMenuItem submenu(const char* id, const char* label);
  static RolltuiMenuItem toggle(const char* id, const char* label, bool checked);
  static RolltuiMenuItem choice(const char* id, const char* label, const char* value);  /* options: children.push_back */
  static RolltuiMenuItem input(const char* id, const char* label, const char* value = "");
  static RolltuiMenuItem input(const char* id, const char* label, RolltuiInputSpec spec, const char* value = "");
#endif
} RolltuiMenuItem;

/* Sets an item's kind, id and label in one call, releasing whatever they held — the C form of
 * the builders above, so a C host builds an item the way a C++ one does. `shortcut` may be
 * NULL with `shortcut_len` 0. */
void rolltui_menu_item_set(RolltuiMenuItem* it, unsigned char kind, const char* id, size_t id_len, const char* label,
                           size_t label_len, const char* shortcut, size_t shortcut_len);

void rolltui_menu_item_init(RolltuiMenuItem* it);
void rolltui_menu_item_release(RolltuiMenuItem* it); /* everything below and inside; leaves it clean */
void rolltui_menu_item_copy(RolltuiMenuItem* to, const RolltuiMenuItem* from);
int rolltui_menu_item_equal(const RolltuiMenuItem* a, const RolltuiMenuItem* b);
RolltuiMenuItem* rolltui_menu_item_new(void);
void rolltui_menu_item_free(RolltuiMenuItem* it); /* a no-op on NULL */

RolltuiMenuItem* rolltui_menu_list_add(RolltuiMenuItemList* l); /* an EMPTY child, appended */
void rolltui_menu_list_clear(RolltuiMenuItemList* l);           /* frees every child */
void rolltui_menu_list_release(RolltuiMenuItemList* l);         /* …and the array */
void rolltui_menu_list_copy(RolltuiMenuItemList* to, const RolltuiMenuItemList* from);

#ifdef __cplusplus
/* ---- the C++ special members of the structs above (Phase 17 m3) ---------------------------
 * Each one is a CALLER of a C function declared above it, so "release this subtree" has one
 * implementation and a destructor reaches it rather than being a second mechanism.
 *
 * They were out-of-line in `rolltui/MenuTree.cpp` until the C++ binding was deleted. They are not
 * part of that binding — they are what makes "the C++ type IS the C struct" true (Phase 14's
 * one-definition rule), so they had to keep a home; `inline`, beside the declarations they
 * implement, is that home and removes the last C++ translation unit from the library. */
inline bool RolltuiInputSpec::operator==(const RolltuiInputSpec& o) const {
  return rolltui_input_spec_equal(this, &o) != 0;
}

inline RolltuiMenuItemList::~RolltuiMenuItemList() { rolltui_menu_list_release(this); }

inline RolltuiMenuItemList& RolltuiMenuItemList::operator=(RolltuiMenuItemList&& o) noexcept {
  if (this != &o) {
    rolltui_menu_list_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}

inline void RolltuiMenuItemList::push_back(RolltuiMenuItem&& c) { *rolltui_menu_list_add(this) = std::move(c); }

inline void RolltuiMenuItemList::clear() { rolltui_menu_list_clear(this); }

inline bool RolltuiMenuItemList::operator==(const RolltuiMenuItemList& o) const {
  if (n != o.n) return false;
  for (std::size_t i = 0; i < n; ++i)
    if (!rolltui_menu_item_equal(v[i], o.v[i])) return false;
  return true;
}

inline RolltuiMenuItem::RolltuiMenuItem() = default;

inline bool RolltuiMenuItem::operator==(const RolltuiMenuItem& o) const {
  return rolltui_menu_item_equal(this, &o) != 0;
}
inline RolltuiInputSpec RolltuiInputSpec::clone() const {
  RolltuiInputSpec out;
  rolltui_input_spec_copy(&out, this);
  return out;
}
inline RolltuiMenuItem RolltuiMenuItem::clone() const {
  RolltuiMenuItem out;
  rolltui_menu_item_copy(&out, this);
  return out;
}
inline RolltuiMenuItem RolltuiMenuItem::action(const char* id, const char* label, const char* shortcut) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Action), id, std::strlen(id), label, std::strlen(label), shortcut,
                        shortcut ? std::strlen(shortcut) : 0);
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::submenu(const char* id, const char* label) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Submenu), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::choice(const char* id, const char* label, const char* value) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Choice), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  it.value = value;
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::input(const char* id, const char* label, const char* value) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Input), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  it.value = value;
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::input(const char* id, const char* label, RolltuiInputSpec spec, const char* value) {
  RolltuiMenuItem it = input(id, label, value);
  it.spec = std::move(spec);
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::toggle(const char* id, const char* label, bool checked) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Toggle), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  it.checked = static_cast<unsigned char>(checked);
  return it;
}

#endif

/* ========================================================================================
 * effects — a host registers effect kinds and holds an effect map
 * ======================================================================================== */
/* Everything a kind is allowed to know about the cell it is answering for. */
typedef struct RolltuiEffectCell {
  unsigned long long elapsed_ms ROLLTUI_DEFAULT(0); /* since the span entered the state */
  int index ROLLTUI_DEFAULT(0);                     /* 0-based, within the span */
  int length ROLLTUI_DEFAULT(1);                    /* the span's length in cells */
  double fraction ROLLTUI_DEFAULT(0);               /* Progress: 0..1 */
  RolltuiStyle base;                                /* the cell's style as drawn */
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);  /* this terminal's East Asian ambiguous width */
} RolltuiEffectCell;

/* THE GLYPH IS INLINE, WITH A STATED REFUSAL RATHER THAN A SPILL — a deliberate answer to
 * CLAUDE.md's strategy 1, and different from `RolltuiCell`'s on purpose. A cell must hold
 * whatever a DOCUMENT contains, so it spills; an effect glyph is one frame of a cycle a
 * theme author or a host WROTE, so a bound is a constraint on that file rather than on a
 * user's data. Thirty-two bytes is past every emoji ZWJ sequence Unicode defines, and
 * anything longer is REFUSED AND COUNTED in `glyphs_refused` — the same mechanism, and the
 * same report, that property 1 already uses for a glyph of the wrong width. Never silent.
 *
 * `glyph_len` is the length the kind ASKED for, even when it is past the cap, which is
 * what lets the applier tell "too long" from "as long as it fits". */
#define ROLLTUI_EFFECT_GLYPH_MAX 32
typedef struct RolltuiEffectOut {
  unsigned char has_glyph ROLLTUI_DEFAULT(0);
  unsigned char has_style ROLLTUI_DEFAULT(0);
  RolltuiStyle style;
  size_t glyph_len ROLLTUI_DEFAULT(0);
  char glyph[ROLLTUI_EFFECT_GLYPH_MAX];

#ifdef __cplusplus
  // A kind says what it wants drawn. Always records the full length, so an override past
  // the cap is refused by the applier rather than silently truncated into a valid-looking
  // narrower glyph. Pointer and length: rolltui's own shape (Phase 19 m2).
  void set_glyph(const char* g, std::size_t n) {
    has_glyph = 1;
    glyph_len = n;
    if (n <= ROLLTUI_EFFECT_GLYPH_MAX && n != 0) std::memcpy(glyph, g, n);
  }
#endif
} RolltuiEffectOut;

/* One frame of a glyph cycle: a BORROW of bytes the owning map keeps, valid for as long as
 * that map is not changed. */
typedef struct RolltuiEffectFrame {
  const char* bytes;
  size_t len;
} RolltuiEffectFrame;

/* ONE THEME SPEC, owned by the `RolltuiEffectMap` it lives in. Every pointer here borrows
 * that map's storage and is valid until the map next changes — the same stated window
 * `rolltui_effect_kind_name` has, and for the same reason. */
typedef struct RolltuiEffectSpec {
  const char* kind;
  size_t kind_len;
  const unsigned char* roles; /* rolltui::Role values, one byte each */
  size_t role_count;          /* NEVER zero: the map substitutes the fallback it was handed */
  /* …and what the spec itself was GIVEN, which is 0 when it named none. The two are
   * separate because collapsing them would lose a distinction a file can make: a
   * serialiser must write no "roles" key for a spec that had none, and the substituted
   * fallback is indistinguishable from a spec that named exactly that one role. The
   * applier reads `role_count`; anything writing the theme back out reads this. */
  size_t own_role_count;
  const RolltuiEffectFrame* frames;
  size_t frame_count;
  int period_ms; /* one full cycle; 0 or less: a STILL effect, no tick */
  int width;     /* shimmer: the sweeping window, in cells */
  int steps;     /* how many distinct pictures a period has (0 → the kind's own) */
  unsigned char backward;

#ifdef __cplusplus
  std::size_t roles_size() const { return role_count; }
  unsigned char role(std::size_t i) const { return roles[i % role_count]; }
#endif
} RolltuiEffectSpec;

/* ---- what a THEME carries, owned in C -------------------------------------------------- */
/* A map of STATE → the specs that state looks like while it lasts. `states` is the caller's
 * state vocabulary size — the C indexes with it and never learns a state's name — and
 * `fallback_role` is the role a spec with none of its own picks, handed over once here so
 * that no rung of this file has an opinion about it.
 *
 * OWNED, LONG-LIVED (CLAUDE.md's strategy 4): one map per `rolltui::Theme`, which frees it. */
typedef struct RolltuiEffectMap RolltuiEffectMap;
RolltuiEffectMap* rolltui_effect_map_new(size_t states, unsigned char fallback_role);
void rolltui_effect_map_free(RolltuiEffectMap* m);
int rolltui_effect_map_empty(const RolltuiEffectMap* m);
/* A BORROW, valid until the map next changes. NULL for an index past the state's specs. */


typedef struct RolltuiEffectReport {
  int marks_drawn;    /* marks the theme had an effect for */
  int cells_touched;
  int glyphs_refused; /* overrides dropped: wrong width, or past the glyph cap */
} RolltuiEffectReport;

/* `ctx` is whatever the host handed to `rolltui_effect_register`; `host` is whatever the
 * caller handed to `rolltui_effects_apply` (the C++ side passes its `const Theme*`). The C
 * dereferences neither. */
typedef void (*RolltuiEffectFn)(void* ctx, const RolltuiEffectSpec* spec, const RolltuiStyle* styles,
                                const void* host, const RolltuiEffectCell* in, RolltuiEffectOut* out);

/* ---- THE EFFECT-STATE VOCABULARY (Phase 17, 2026-09-05) ---------------------------------
 * The same move, and for the same reason, as `ROLLTUI_ROLE_LIST` in `rolltui_style.h`: these
 * five names were an `enum class` plus a parallel array in `rolltui/Effects.cpp`, so a C
 * consumer could reach neither and every one that needed them copied the list. Both spellings
 * now expand this one.
 *
 * A widget MARKS a span with a state and stops; the theme maps state -> effect as data in its
 * file. That mapping is read from a theme file BY NAME, which is exactly why the names have to
 * be reachable from the C that does the reading.
 *
 * ORDER IS ABI — `none` must stay 0, because a zeroed mark means "not marked". */
/* THREE SPELLINGS, ONE LIST: the name a theme FILE uses ("waiting"), the C constant
 * (ROLLTUI_EFFECT_STATE_WAITING) and the identifier C++ reads best (EffectState::Waiting).
 * A third column rather than a second list, because the whole point is that adding a state
 * is one edit. */
#define ROLLTUI_EFFECT_STATE_LIST(X) \
  X(none, NONE, None) \
  X(waiting, WAITING, Waiting) \
  X(streaming, STREAMING, Streaming) \
  X(progress, PROGRESS, Progress) \
  X(flash, FLASH, Flash)

typedef enum RolltuiEffectState {
#define ROLLTUI_EFFECT_STATE_ENUM_(lower, UPPER, Camel) ROLLTUI_EFFECT_STATE_##UPPER,
  ROLLTUI_EFFECT_STATE_LIST(ROLLTUI_EFFECT_STATE_ENUM_)
#undef ROLLTUI_EFFECT_STATE_ENUM_
  ROLLTUI_EFFECT_STATE_COUNT
} RolltuiEffectState;


/* The state of that name, or -1 when there is none — what a theme LOADER needs. */
int rolltui_effect_state_from_name(const char* name, size_t len);

#define ROLLTUI_EFFECT_OK 0
#define ROLLTUI_EFFECT_NO_NAME 1
#define ROLLTUI_EFFECT_NO_FN 2
#define ROLLTUI_EFFECT_IS_BUILTIN 3 /* rung 1 is never shadowed */
#define ROLLTUI_EFFECT_DUPLICATE 4



/* ---- working memory -------------------------------------------------------------------- */
/* The grapheme buffer and the Unicode scratch the glyph kinds need to measure their own
 * frames. One handle per thread, made once, grown over the first few calls and never
 * again — the same shape as `RolltuiUnicodeScratch` and `RolltuiDiffScratch`, and for the
 * same reason: "this function needs somewhere to work" is a missing handle, not a new
 * allocation strategy (CLAUDE.md). */
typedef struct RolltuiEffectScratch RolltuiEffectScratch;
RolltuiEffectScratch* rolltui_effect_scratch_new(void);
void rolltui_effect_scratch_free(RolltuiEffectScratch* s);

/* Called once per kind the map names that NOTHING answers for, with its name as a borrow
 * valid for the call. A host says it out loud; the C never judges a kind (Effects.hpp). */
typedef void (*RolltuiEffectUnknownFn)(void* ctx, const char* kind, size_t len);

/* A mark names its state as an int the frame stored and never interpreted
 * (rolltui_screen.h), so the C indexes the map with it and still knows nothing about the
 * state vocabulary — which lives in `Effects.hpp` alone. A state index outside the map's
 * own `states` draws nothing.
 *
 * Called by a host AFTER the whole screen has composed and before the frame diff.
 * `now_ms` is any monotonic millisecond clock. `rep` may not be NULL; `on_unknown` may. */
void rolltui_effects_apply(RolltuiFrame* f, RolltuiEffectScratch* s, const RolltuiStyle* styles, const void* host,
                           const RolltuiEffectMap* map, unsigned long long now_ms, int ambiguous_wide,
                           RolltuiEffectReport* rep, RolltuiEffectUnknownFn on_unknown, void* unknown_ctx);

/* The interval at which this frame must be redrawn for its motion, or 0 when nothing is
 * marked, when the theme maps nothing to what is marked, or when nothing mapped MOVES.
 * Clamped to at least 16 ms so a long span cannot ask for a wakeup per millisecond. */
int rolltui_effects_tick_ms(const RolltuiFrame* f, const RolltuiEffectMap* map);


/* ========================================================================================
 * json — RolltuiJsonValue: a public type (a theme preset holds one), so its API is public
 * ======================================================================================== */
#define ROLLTUI_JSON_NULL 0
#define ROLLTUI_JSON_BOOL 1
#define ROLLTUI_JSON_NUMBER 2
#define ROLLTUI_JSON_STRING 3
#define ROLLTUI_JSON_ARRAY 4
#define ROLLTUI_JSON_OBJECT 5

typedef struct RolltuiJsonValue RolltuiJsonValue;

/* One member of an object: a key and its OWNED value. `obj` below keeps these in insertion
 * order, the same rule `rolltui::json::Value::obj` states (a theme's role list round-trips
 * in the order the author wrote it). */
typedef struct RolltuiJsonMember {
  RolltuiStr key;
  RolltuiJsonValue* value ROLLTUI_DEFAULT(nullptr); /* OWNED; never NULL on a complete value */
} RolltuiJsonMember;

/* A plain struct with every member always present, exactly the shape `rolltui::json::Value`
 * already is (it is not a real tagged union there either — see that header): `kind` says
 * which of `str`/`arr`/`obj` is MEANINGFUL, but `rolltui_json_free`/`_clone`/`_equal`
 * deliberately do not gate on it, so retagging a value (as `rolltui_json_set` always does)
 * can never orphan a buffer the other two left behind. */
struct RolltuiJsonValue {
  unsigned char kind ROLLTUI_DEFAULT(ROLLTUI_JSON_NULL);
  unsigned char b ROLLTUI_DEFAULT(0);
  double num ROLLTUI_DEFAULT(0);
  RolltuiStr str;
  /* ARRAY children: an owned array of owned value pointers, laid out as `RolltuiPtrVec`
   * (rolltui_str.h) and using its GROWING AMORTISED mechanics — the same choice
   * `RolltuiMenuItemList`/`RolltuiDocument` made for the same reason: an element's address
   * never moves, and a growth relocates one pointer per element rather than a whole node. */
  RolltuiJsonValue** arr ROLLTUI_DEFAULT(nullptr);
  size_t arr_n ROLLTUI_DEFAULT(0);
  size_t arr_cap ROLLTUI_DEFAULT(0);
  /* OBJECT members: an owned array of owned member pointers, insertion order kept, same
   * layout and reason as `arr` above. */
  RolltuiJsonMember** obj ROLLTUI_DEFAULT(nullptr);
  size_t obj_n ROLLTUI_DEFAULT(0);
  size_t obj_cap ROLLTUI_DEFAULT(0);
};

/* ---- construction: each an OWNED, LONG-LIVED node the caller frees (directly, or by
 * handing it to `rolltui_json_set`/`_array_push`, which then own it). ---------------------- */
RolltuiJsonValue* rolltui_json_null(void);
RolltuiJsonValue* rolltui_json_bool(int b);
RolltuiJsonValue* rolltui_json_number(double n);
RolltuiJsonValue* rolltui_json_string(const char* s, size_t len);
RolltuiJsonValue* rolltui_json_array(void);
RolltuiJsonValue* rolltui_json_object(void);

void rolltui_json_free(RolltuiJsonValue* v); /* recursive; a no-op on NULL */
/* A deep copy the caller owns; NULL in, NULL out (mirrors `Value`'s copy constructor). */
RolltuiJsonValue* rolltui_json_clone(const RolltuiJsonValue* v);
/* Deep, order-sensitive structural equality (mirrors `Value`'s defaulted `operator==`, which
 * compares every member unconditionally rather than only the ones `kind` says are live). */
int rolltui_json_equal(const RolltuiJsonValue* a, const RolltuiJsonValue* b);

int rolltui_json_is_null(const RolltuiJsonValue* v);
int rolltui_json_is_bool(const RolltuiJsonValue* v);
int rolltui_json_is_number(const RolltuiJsonValue* v);
int rolltui_json_is_string(const RolltuiJsonValue* v);
int rolltui_json_is_array(const RolltuiJsonValue* v);
int rolltui_json_is_object(const RolltuiJsonValue* v);

/* Typed reads with defaults; never fail. A BORROW valid as long as `v` (or `def`) is. */
const char* rolltui_json_as_string(const RolltuiJsonValue* v, const char* def, size_t def_len, size_t* out_len);
double rolltui_json_as_number(const RolltuiJsonValue* v, double def);
int rolltui_json_as_bool(const RolltuiJsonValue* v, int def);

/* Object lookup; a BORROW, and never NULL — a static Null (also a BORROW, valid forever)
 * when `v` is not an object or the key is absent, the same "missing keys are Null and
 * chainable" rule `Value::get` states. */
const RolltuiJsonValue* rolltui_json_get(const RolltuiJsonValue* v, const char* key, size_t key_len);
int rolltui_json_has(const RolltuiJsonValue* v, const char* key, size_t key_len);
/* Object insert-or-replace. ALWAYS turns `v` into an object (matches `Value::set` exactly,
 * including on a `v` that was something else — nothing is cleared, which is safe here only
 * because free/clone/equal above never gate on `kind`). TAKES OWNERSHIP of `child`; a
 * replaced value is freed. Returns a BORROW of the now-stored child. */
RolltuiJsonValue* rolltui_json_set(RolltuiJsonValue* v, const char* key, size_t key_len, RolltuiJsonValue* child);
/* Removes `key` if the object has it, freeing the value; 1 when something was removed.
 * Order-preserving, like the `std::remove_if` it replaces.
 *
 * IT HAS NO CALLER IN THE LIBRARY as of 2026-09-05 — the preset-directory migration that
 * asked for it was retired — and it is KEPT anyway, on a stated reason rather than inertia:
 * it is the fourth of `get`/`has`/`set`/`erase`, and an object API that can add a key but
 * not remove one is a hole a consumer has to work around with a rebuild. */
int rolltui_json_object_erase(RolltuiJsonValue* v, const char* key, size_t key_len);

size_t rolltui_json_array_size(const RolltuiJsonValue* v);
RolltuiJsonValue* rolltui_json_array_at(const RolltuiJsonValue* v, size_t i); /* BORROW; NULL out of range */
/* Appends. TAKES OWNERSHIP of `child`. `v` must already be an array (`rolltui_json_array()`)
 * — no coercion, matching `.arr.push_back()` on the C++ side never touching `.kind` either. */
void rolltui_json_array_push(RolltuiJsonValue* v, RolltuiJsonValue* child);

/* Generic object iteration (unordered lookup by key is `get`/`has` above; this is for a
 * caller that must see every member, e.g. AppProfile's "unknown key" scan). BORROWS. */
size_t rolltui_json_object_size(const RolltuiJsonValue* v);
const char* rolltui_json_object_key_at(const RolltuiJsonValue* v, size_t i, size_t* len);
RolltuiJsonValue* rolltui_json_object_value_at(const RolltuiJsonValue* v, size_t i);

/* Parses `text`. Returns an OWNED value the caller frees, or NULL on failure. `error` may be
 * NULL when the caller does not care; otherwise it is cleared on entry and set to
 * "line N: message" on the first failure only, and left empty on success. */
RolltuiJsonValue* rolltui_json_parse(const char* text, size_t len, RolltuiStr* error);
/* Serialises deterministically into `out`, REPLACING its contents. indent = 0 -> single
 * line. */
void rolltui_json_dump(const RolltuiJsonValue* v, int indent, RolltuiStr* out);


/* ========================================================================================
 * theme — a host holds a style table
 * ======================================================================================== */
/* ---- THE MODE AND DEPTH VOCABULARY (Phase 17 m2a, 2026-09-05) -----------------------------
 *
 * THIS FILE'S OWN TOP COMMENT SAID IT DELIBERATELY DOES NOT KNOW THESE NAMES, AND THAT WAS
 * RIGHT WHEN IT WAS WRITTEN. It read: *"a name a config file and a `--color-depth` flag both
 * spell is a vocabulary, and a vocabulary written down twice is a second thing to drift. So
 * `detect_color_depth` ... stays one level up, and this file is handed the answer."* True
 * while the library was C++ with a C core; false once the library IS the C, because
 * `Theme.cpp` is being deleted and the vocabulary would go with it. This is the same reversal
 * `ROLLTUI_ROLE_LIST` (rolltui_style.h) and `ROLLTUI_EFFECT_STATE_LIST` (rolltui_effects.h)
 * already made, for the same reason and on the same evidence: a vocabulary the C refuses to
 * carry does not disappear, it relocates into every caller that cannot reach it.
 *
 * FOUR SPELLINGS EXISTED WHEN THIS WAS WRITTEN, and the fourth is the one that matters:
 *   1. `Presets.cpp`  valid_depth_setting / depth_from_setting  (the "auto" layer)
 *   2. `Theme.cpp`    detect_color_depth / color_depth_name
 *   3. this header's own prose
 *   4. `rolltui/tests/theme_test.cpp:297-317` — a VERBATIM 13-line REIMPLEMENTATION of both
 *      of (2), in an anonymous namespace, which lines 498-506 then assert against. That file
 *      has no `using namespace rolltui` and does not include `Theme.hpp`, so it cannot reach
 *      the real function at all: `rolltui::detect_color_depth` ships in `studio.cpp` (5 call
 *      sites) and `TuiFrontend.cpp` and is tested by NOBODY. Nine assertions covering a path
 *      nothing runs — the same shape found in the same file one day earlier for the role
 *      names, and the third instance of it in this phase.
 *
 * ORDER IS ABI: the ordinal is what `rolltui_sgr`, `rolltui_color_downgrade` and every
 * renderer are handed. Mono must stay 0 and TrueColor last — `rolltui_color_downgrade`
 * compares against the constants, not against a count. */
#define ROLLTUI_DEPTH_LIST(X) \
  X("mono", MONO, Mono) \
  X("16", ANSI16, Ansi16) \
  X("256", ANSI256, Ansi256) \
  X("truecolor", TRUECOLOR, TrueColor)

/* The one ALIAS, and it belongs to the ENVIRONMENT rather than to the file format: COLORTERM
 * and ROLL_COLOR_DEPTH accept "24bit", a preset file's "depth" does not, and
 * `color_depth_name` must answer "truecolor" and only "truecolor". Keeping the two apart is
 * not pedantry — a preset that stored "24bit" would round-trip to "truecolor" and stop
 * matching itself, so `modified()` would report a change nobody made. Only
 * `rolltui_detect_color_depth` reads this list. */
#define ROLLTUI_DEPTH_ENV_ALIAS_LIST(X) X("24bit", TRUECOLOR)

typedef enum RolltuiColorDepth {
#define ROLLTUI_DEPTH_ENUM_(lower, UPPER, Camel) ROLLTUI_DEPTH_##UPPER,
  ROLLTUI_DEPTH_LIST(ROLLTUI_DEPTH_ENUM_)
#undef ROLLTUI_DEPTH_ENUM_
  ROLLTUI_DEPTH_COUNT
} RolltuiColorDepth;

#define ROLLTUI_MODE_LIST(X) \
  X("dark", DARK, Dark) \
  X("light", LIGHT, Light)

typedef enum RolltuiThemeMode {
#define ROLLTUI_MODE_ENUM_(lower, UPPER, Camel) ROLLTUI_MODE_##UPPER,
  ROLLTUI_MODE_LIST(ROLLTUI_MODE_ENUM_)
#undef ROLLTUI_MODE_ENUM_
  ROLLTUI_MODE_COUNT
} RolltuiThemeMode;

/* BORROWS a static literal; `*len` may be NULL. An out-of-range depth reads back as "mono"
 * and an out-of-range mode as "dark", which is what the C++ `color_depth_name` did and what a
 * zeroed byte means. */
const char* rolltui_color_depth_name(unsigned char depth, size_t* len);

/* The depth/mode of that name, or -1 when there is none. Neither accepts "auto" (that is the
 * SETTING layer below, not a depth) and neither accepts the env alias above. */
int rolltui_color_depth_from_name(const char* name, size_t len);
int rolltui_theme_mode_from_name(const char* name, size_t len);

/* THE SETTING layer: what a preset file and `--color-depth`/`--theme-mode` accept, which is
 * every name above PLUS "auto" (resolve it, do not store it). `rolltui::valid_depth_setting`
 * and `valid_mode_setting` were these, and `rolltui_theme_preset_parse` took them as
 * CALLBACKS precisely because a C file could not spell the names; it can now, and the
 * callback parameters stay only so a host may narrow what IT accepts. */
int rolltui_color_depth_setting_valid(const char* s, size_t len);
int rolltui_theme_mode_setting_valid(const char* s, size_t len);

/* COLORTERM=truecolor|24bit -> TrueColor; TERM containing "256color" -> Ansi256; TERM=dumb or
 * empty -> Mono; else Ansi16. `force` (ROLL_COLOR_DEPTH) wins when set and valid. Any
 * argument may be NULL. `rolltui::detect_color_depth`'s port, verbatim. */
unsigned char rolltui_detect_color_depth(const char* colorterm, const char* term, const char* force);

/* "#rrggbb" | "none" | "0".."255". 1 on success, 0 when it is not a colour. */
int rolltui_color_parse(const char* text, size_t len, RolltuiStyleColor* out);

/* The colour in the same spelling. "#rrggbb" is the longest, so seven bytes plus nothing —
 * the result is NOT terminated and the length is returned. */
#define ROLLTUI_COLOR_STRING_MAX 8
size_t rolltui_color_to_string(RolltuiStyleColor c, char* out, size_t cap);


/* The SGR sequence that selects `style` at `depth`, into `out`. Always starts from a reset,
 * so a cell's style never depends on the previous cell's. The longest is
 * "\x1b[0;1;2;3;4;7;38;2;255;255;255;48;2;255;255;255m" — 48 bytes, and the cap is a
 * constraint on this file rather than on a caller's data. */
#define ROLLTUI_SGR_MAX 64
size_t rolltui_sgr(const RolltuiStyle* style, unsigned char depth, char* out, size_t cap);


/* The mode a background implies: relative luminance (sRGB linearised, Rec. 709 weights)
 * above 0.5 is light, anything else — including a colour that is not rgb — is dark. */
unsigned char rolltui_mode_for_background(RolltuiStyleColor bg);

/* A BORROW into the caller's own table, valid exactly as long as `styles` is. NULL when
 * `styles` is NULL or `role` is out of range — a bad ordinal is a caller bug, not a crash. */
const RolltuiStyle* rolltui_theme_style(const RolltuiStyle* styles, size_t role_count, unsigned char role);

/* A role or effect-state NAME TABLE, handed to the loader/dumper once per call — see this
 * header's comment above for why a table crosses instead of the vocabulary moving in.
 * `role_names[i]`/`state_names[i]` are NUL-terminated (every string this boundary already
 * hands across is — a parsed JSON string via `RolltuiStr`, and a C++ string literal both
 * are), so nothing here carries a parallel length array: `strlen` is cheap at theme-load
 * rate (never per frame) and a second array is a second thing that could disagree with the
 * first. Every pointer is a BORROW for the one call.
 *
 *   role_names / role_count   `rolltui::Role`'s declaration order (`Style.hpp`); `out_styles`
 *                              below is filled positionally against this SAME order.
 *   text_role                  the ordinal "text" resolves to — the role every other
 *                              inherits from, and the one this file must special-case
 *                              (`ROLLTUI_ROLE_DEFAULT_TEXT` one level up, in `rolltui_style.h`).
 *   state_names / state_count  `EffectState`'s declaration order (`Effects.hpp`); index 0
 *                              ("none") never matches a theme file's "effects" key, the same
 *                              way `rolltui::read_effects` already rejected it.
 *   fallback_effect_role        the role byte an effect spec with none of its own picks —
 *                              handed to `rolltui_effect_map_new` exactly once, here, the
 *                              same value `rolltui::EffectMap`'s default constructor already
 *                              hands it (`Role::accent_1`).
 */
typedef struct RolltuiThemeVocab {
  const char* const* role_names;
  size_t role_count;
  size_t text_role;
  const char* const* state_names;
  size_t state_count;
  unsigned char fallback_effect_role;
} RolltuiThemeVocab;

/* THE LIBRARY'S OWN, and the reason it can exist is the reason this file's "what it does not
 * know" note above is now half retracted (Phase 17 m2a). The vocab was invented because a C
 * file could not name a role or an effect state; both are C since the role and effect-state
 * X-macros landed, so the library can hand a caller its own table instead of asking for one.
 *
 * SIX consumers were building or reaching for a table by then: `Theme.cpp` (the real one),
 * `ThemeAnalysis.cpp` and `ThemeGen.cpp` and `Presets.cpp` (each forward-declaring
 * `rolltui::theme_vocab()` across a translation unit), `theme_test.cpp` (a private duplicate),
 * and `tools/theme_editor.cpp`, whose conversion forward-declared it too — a `tools/` file
 * reaching into `Theme.cpp`, which m2c deletes.
 *
 * The PARAMETER stays on every function that takes one: a host with its own roles is what the
 * vocab was for. This is the default, not a policy. BORROWS static storage. */
const RolltuiThemeVocab* rolltui_theme_default_vocab(void);


/* Fills `styles[0..role_count)` (CALLER-FILLED: `styles` is the caller's own table, the
 * `Theme::styles` array itself — no allocation) for the named built-in theme, and returns a
 * freshly built, OWNED effect map the caller adopts (`rolltui::EffectMap`'s adopting
 * constructor) or frees with `rolltui_effect_map_free`. NULL, with `styles` untouched, when
 * the name is unknown OR `role_count` does not match this file's own table (a defensive
 * invariant check: `rolltui::Style.hpp`'s `kRoleCount` and this file's role tables must agree,
 * and disagreement should read as "this theme doesn't exist" rather than write past the end
 * of a caller's array) — mirroring `rolltui::builtin_theme`'s "unknown name -> nullptr". */
RolltuiEffectMap* rolltui_theme_builtin_fill(const char* name, size_t name_len, RolltuiStyle* styles,
                                             size_t role_count);

/* ---- the load report: mirrors rolltui::ThemeLoadReport field for field -------------------- */
typedef struct RolltuiThemeReport {
  RolltuiStr error; /* non-empty: the file was unusable */
  RolltuiStr* missing_roles; size_t missing_roles_n, missing_roles_cap; /* GROWING AMORTISED */
  RolltuiStr* unknown_keys;  size_t unknown_keys_n,  unknown_keys_cap;  /* GROWING AMORTISED */
  RolltuiStr* bad_values;    size_t bad_values_n,    bad_values_cap;    /* GROWING AMORTISED */
} RolltuiThemeReport;

/* Frees everything and zeroes the struct — safe on an already-zeroed one and on repeated
 * calls, the same "reset, not just release" contract `rolltui_app_profile_report_release`
 * states. Zero-initialise a fresh one (`RolltuiThemeReport r = {0};`) before first use. */
void rolltui_theme_report_release(RolltuiThemeReport* r);

/* ---- the loader ----------------------------------------------------------------------------
 * Parses a theme already as a TREE — the caller either parsed the file's text with
 * `rolltui_json_parse` directly, or already held a `json::Value` and converted it
 * (`rolltui::load_theme`'s two overloads do exactly one of these each). Mirrors
 * `rolltui::load_theme(const json::Value&, ThemeMode, ThemeLoadReport&)` exactly, MINUS
 * "meta" and the `Theme` object itself — see this header's top comment for why both stay
 * outside.
 *
 * `report` is RESET by this call (as if freshly zero-initialised) whether it succeeds or
 * fails, the same contract `rolltui_app_profile_parse` states. Returns NULL only when `root`
 * is not a usable theme object at all (`report->error` explains: not a JSON object, or no
 * "roles" object) — `out_styles`/`out_name` are untouched in that case. Every other problem
 * still produces a usable theme: `out_styles[0..vocab->role_count)` is filled in full (the
 * "text" style substituted for any role the file did not define, `report->missing_roles`
 * naming each), `out_name` gets the theme's own "name" ("unnamed" when absent or not a
 * string), and the return is a freshly built, OWNED, non-NULL effect map (empty — a still UI
 * — for a file with no usable "effects" key), with every problem in `report`. */
RolltuiEffectMap* rolltui_theme_load(const RolltuiJsonValue* root, int mode, const RolltuiThemeVocab* vocab,
                                     RolltuiStyle* out_styles, RolltuiStr* out_name, RolltuiThemeReport* report);

/* ---- the dumper ----------------------------------------------------------------------------
 * Builds a fresh, OWNED tree (caller frees with `rolltui_json_free`) with a "roles" object
 * and, when anything is marked, an "effects" object — mirrors `style_to_json`+
 * `effect_to_json`+`effects_to_json`+the "roles"/"effects" halves of
 * `theme_to_json_value`/`theme_pair_to_json_value` exactly. Neither "name" nor "meta" is set
 * here (see this header's top comment); the C++ shim pulls "roles"/"effects" out of the
 * result to set them, in order, after both.
 *
 * `light_styles`/`light_effects` NULL together dump ONE variant plainly
 * (`style_to_json(s, nullptr)`); non-NULL dumps BOTH as one object, a role or attribute
 * written as {"dark":..,"light":..} only where the two differ. `light_effects` is accepted
 * but never consulted: motion is the THEME's, not the terminal background's (`Theme.hpp`),
 * so "effects" is always `dark_effects` alone — the same asymmetry
 * `theme_pair_to_json_value` already has by taking a whole light `Theme` and reading only
 * its `.styles`. */
RolltuiJsonValue* rolltui_theme_dump(const RolltuiStyle* dark_styles, const RolltuiEffectMap* dark_effects,
                                     const RolltuiStyle* light_styles, const RolltuiEffectMap* light_effects,
                                     const RolltuiThemeVocab* vocab);


/* ========================================================================================
 * unicode — the library's Unicode algorithms; three functions a host reaches are public, the rest are the wrap engine's
 * ======================================================================================== */
/* One decoded scalar. Decoding is TOTAL: a malformed byte becomes U+FFFD with `length` 1 and
 * `valid` 0, so every byte of the input is accounted for exactly once and a byte offset is
 * always recoverable. */
typedef struct RolltuiDecodedChar {
  RolltuiCodepoint cp;
  size_t offset; /* byte offset into the source */
  size_t length; /* bytes consumed (1 for an invalid byte) */
  unsigned char valid;
} RolltuiDecodedChar;

/* ---- working memory ---------------------------------------------------------------------- */
/* THE GROWING BUFFERS THESE ALGORITHMS NEED, owned by the caller and reused across calls.
 *
 * The six functions marked below need somewhere to decode into, mark boundaries in, and build
 * line-break units in. There were three ways to give them that and only one is simple:
 *   - hidden per-thread buffers (what the C++ has, twenty-five of them across the library) —
 *     invisible state with a lifetime nobody owns;
 *   - allocate per call — an allocation on the draw path, which Phase 13 spent itself removing;
 *   - **hand them a buffer, which is what every other handle in this port already does.**
 * One handle per thread, made once and reused forever, is all a host needs: after the first
 * few calls it never grows again, so the draw path allocates NOTHING and there is no spill
 * case, no stack-size question and nothing retained that an exit would have to clean up.
 *
 * It holds one buffer per ROLE rather than one shared pool, so a function that calls another
 * (graphemes → boundaries, display_width → graphemes) cannot alias its own scratch. */
typedef struct RolltuiUnicodeScratch RolltuiUnicodeScratch;
RolltuiUnicodeScratch* rolltui_u_scratch_new(void);
void rolltui_u_scratch_free(RolltuiUnicodeScratch* s);

/* Decodes into three parallel caller arrays, each of which must hold at least `len` entries —
 * decoding is total and a malformed byte is one scalar of length 1, so the count can never
 * exceed the byte count. Returns the number of scalars. Kept as parallel arrays rather than an
 * array of `RolltuiDecodedChar` because the wrap engine wants a contiguous code-point array
 * and building one out of an array of structs was a copy loop it no longer has (m3). */
size_t rolltui_u_decode_utf8(const char* s, size_t len, RolltuiCodepoint* cp, size_t* offset, size_t* length);
/* The same, into an array of structs, which is what a caller wanting `valid` needs. `out` must
 * hold at least `len` entries. Returns the number of scalars. */
size_t rolltui_u_decode_utf8_chars(const char* s, size_t len, RolltuiDecodedChar* out);

int rolltui_u_display_width(RolltuiUnicodeScratch* s, const char* utf8, size_t len, int ambiguous_wide);

/* ---- sanitising ------------------------------------------------------------------------ */
/* Removes terminal control sequences from text that will be RENDERED. `out` must hold at least
 * `len` bytes — stripping only ever removes. Returns the bytes written. */
size_t rolltui_u_strip_escape_sequences(const char* s, size_t len, char* out);



#define ROLLTUI_MENU_EVENT_NONE 0
#define ROLLTUI_MENU_EVENT_ACTIVATE 1
#define ROLLTUI_MENU_EVENT_TOGGLE 2
#define ROLLTUI_MENU_EVENT_CHOOSE 3
#define ROLLTUI_MENU_EVENT_INPUT 4
#define ROLLTUI_MENU_EVENT_CLOSED 5

typedef struct RolltuiMenuEvent {
  unsigned char kind ROLLTUI_DEFAULT(ROLLTUI_MENU_EVENT_NONE);
  RolltuiStr id;    /* the acted-on item (Choose: the Choice item) */
  RolltuiStr value; /* Choose: the option id; Input: the committed (canonical) text */
  unsigned char checked ROLLTUI_DEFAULT(0); /* Toggle: the new state */
} RolltuiMenuEvent;

void rolltui_menu_event_release(RolltuiMenuEvent* e);

typedef struct RolltuiMenu RolltuiMenu;
/* `editor` is BORROWED and must outlive the menu — `rolltui::Menu` owns it. */
/* The menu OWNS its editor (Phase 17): the typed-field editing a menu does is not optional,
 * and every caller was constructing one to hand in. `rolltui_menu_editor` borrows it back. */
RolltuiMenu* rolltui_menu_new(void);

/* The menu's editor, BORROWED, valid for the menu's life. */
RolltuiInput* rolltui_menu_editor(const RolltuiMenu* m);
void rolltui_menu_free(RolltuiMenu* m);

/* ---- the tree ---------------------------------------------------------------------------------- */
void rolltui_menu_set_root(RolltuiMenu* m, const RolltuiMenuItem* root); /* by COPY; also resets */
RolltuiMenuItem* rolltui_menu_root(RolltuiMenu* m);
/* Depth-first, any level; NULL when absent. */
RolltuiMenuItem* rolltui_menu_find(RolltuiMenu* m, const char* id, size_t len);
/* A Choice's options / a Submenu's items, by COPY, then the flat list and the selection are
 * rebuilt. */
int rolltui_menu_set_options(RolltuiMenu* m, const char* id, size_t len, const RolltuiMenuItemList* options);
/* THE THREE PLAIN SETTERS, each `find` plus one assignment, 0 when no item has that id. They
 * lived in the shim through Phase 15 on the argument that a caller could write `find` itself —
 * true, and the reason it stopped holding is rule 5: with `Windows::menu()` handing back a
 * `RolltuiMenu*`, BOTH hosts write the same three-line wrapper, which is the tell that the API
 * is wrong rather than the consumers (Phase 17 m1c). */
int rolltui_menu_set_value(RolltuiMenu* m, const char* id, size_t len, const char* value, size_t value_len);
int rolltui_menu_set_checked(RolltuiMenu* m, const char* id, size_t len, int checked);
int rolltui_menu_set_enabled(RolltuiMenu* m, const char* id, size_t len, int enabled);

/* ---- navigation state --------------------------------------------------------------------------- */
void rolltui_menu_reset(RolltuiMenu* m);
/* The path from the root down, as a BORROW valid until the menu next navigates. */
const RolltuiMenuItem* rolltui_menu_level(const RolltuiMenu* m);
const RolltuiMenuItem* rolltui_menu_selected_item(const RolltuiMenu* m);
/* The current level's children passing the filter (indices into the level's children), or in
 * palette mode indices into the flattened list. A BORROW, valid until the menu next changes. */
/* "settings › theme", into a caller's string. */
void rolltui_menu_breadcrumb(const RolltuiMenu* m, RolltuiStr* out);
int rolltui_menu_editing(const RolltuiMenu* m);
const char* rolltui_menu_edit_reason(const RolltuiMenu* m, size_t* len);
void rolltui_menu_set_palette(RolltuiMenu* m, int on);

/* THE THIRTEEN ACTION NAMES this widget's two tables are keyed by, handed over once. */
typedef struct RolltuiMenuActions {
  const char* up;
  const char* down;
  const char* page_up;
  const char* page_down;
  const char* first;
  const char* last;
  const char* activate;
  const char* descend;
  const char* ascend;
  const char* back;
  const char* erase;
  /* the `edit` scope, while a typed field is open */
  const char* commit;
  const char* cancel;
  const char* step_up;
  const char* step_down;
  /* …and the INPUT widget's own thirty, because a typed field forwards every key it does not
   * claim to the editor. Handed through rather than duplicated: there is one table of input
   * action names in the whole library and it is `Input.cpp`'s. */
  const RolltuiInputActions* input;
} RolltuiMenuActions;

/* THE HOST'S TEXT VALIDATORS, asked rather than moved. Returns 1 when a validator by that
 * name is registered; `why` is filled (non-empty) when it REFUSES the text. */
typedef int (*RolltuiValidatorFn)(void* ctx, const char* name, size_t nlen, const char* text, size_t tlen,
                                  RolltuiStr* why);

/* ---- the three tree walks (Phase 17 m3) ----------------------------------------------------
 * Pure over a `RolltuiMenuItem` tree, with no widget state in them — which is why they take the
 * ROOT rather than a `RolltuiMenu*`, and why a host can call them on a tree it has not mounted.
 *
 * `apply_shortcuts` fills each item's `shortcut` from the live table. An action NO layout
 * declares is INERT — the table keeps its chords but nothing can emit it — so it gets an EMPTY
 * shortcut rather than its chords: printing them would promise a key that cannot fire, which is
 * the exact lie this function exists to remove.
 *
 * `item_actions` and `unknown_validators` ENUMERATE through a sink, so the caller owns whatever
 * it collects into (rule 1) and nothing is returned by value (rule 2). `item_actions` reports a
 * PAIR per item, which is why it has its own two-string sink rather than `RolltuiPutFn`. */
void rolltui_menu_apply_shortcuts(RolltuiMenuItem* root, const RolltuiBindings* b);

/* One (item id -> action name) pair, and a caller-owned list of them. The m4b shape: a result
 * the library already has goes into the caller's buffer, never through a sink — this took a
 * `RolltuiMenuActionFn` until 2026-09-05, and its one consumer had a struct, a lambda and a
 * `static_cast` around it. Zero-initialise; the C++ destructor releases it. */
typedef struct RolltuiMenuAction {
  RolltuiStr id;
  RolltuiStr action;
} RolltuiMenuAction;

typedef struct RolltuiMenuActionList {
  RolltuiMenuAction* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiMenuActionList() = default;
  RolltuiMenuActionList(const RolltuiMenuActionList&) = delete;
  RolltuiMenuActionList& operator=(const RolltuiMenuActionList&) = delete;
  ~RolltuiMenuActionList();
  const RolltuiMenuAction* begin() const { return v; }
  const RolltuiMenuAction* end() const { return v + n; }
  size_t size() const { return n; }
  bool empty() const { return n == 0; }
#endif
} RolltuiMenuActionList;

void rolltui_menu_action_list_release(RolltuiMenuActionList* l);
/* REPLACES `*out` (reusing its buffers). Depth-first, the tree's own order. */
void rolltui_menu_item_actions(const RolltuiMenuItem* root, RolltuiMenuActionList* out);

/* The LIBRARY'S OWN fifteen (plus the input table they point at), so a consumer can call
 * `handle` without spelling them (Phase 17 m2a). This table used to be in an ANONYMOUS
 * namespace in `Menu.cpp`, which made `rolltui_menu_handle` uncallable from C and from any
 * translation unit that did not include `Menu.hpp` — `menu_test.cpp` had already hand-written
 * its own copy to get past it. BORROWS static storage. */
const RolltuiMenuActions* rolltui_menu_default_actions(void);

void rolltui_menu_handle(RolltuiMenu* m, const RolltuiEvent* e, const RolltuiBindings* bindings,
                         const RolltuiMenuActions* actions, RolltuiMenuEvent* out);

typedef struct RolltuiMenuOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int inset ROLLTUI_DEFAULT(0); /* columns kept clear on each side of the area */
#ifdef __cplusplus
  bool operator==(const RolltuiMenuOptions&) const = default;
#endif
} RolltuiMenuOptions;

void rolltui_menu_set_options_struct(RolltuiMenu* m, const RolltuiMenuOptions* o);
void rolltui_menu_layout(RolltuiMenu* m, RolltuiRect area);

/* THE SEVEN ROLES A DRAW NEEDS, handed in as bytes. Plus the input widget's three, which the
 * editor's own draw takes. */
typedef struct RolltuiMenuRoles {
  unsigned char item;
  unsigned char selected;
  unsigned char breadcrumb;
  unsigned char shortcut;
  unsigned char text_muted;
  unsigned char warning;
  unsigned char scroll_marker;
} RolltuiMenuRoles;

void rolltui_menu_draw(const RolltuiMenu* m, RolltuiFrame* f, RolltuiDrawScratch* draw,
                       const RolltuiStyle* styles, const RolltuiMenuRoles* roles,
                       const RolltuiInputRoles* input_roles, int focused);

/* THE REPORT, transparent like `RolltuiAppProfileReport` and `RolltuiBindingsReport`: exactly
 * `RolltuiStr` values in GROWING AMORTISED arrays, one per `MenuLoadReport` field.
 * Zero-initialise before use. */
typedef struct RolltuiMenuLoadReport {
  RolltuiStr error; /* non-empty: unusable, and rolltui_menu_parse_json returns 0 */
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
} RolltuiMenuLoadReport;

void rolltui_menu_load_report_release(RolltuiMenuLoadReport* r); /* frees everything; zeroes it */
int rolltui_menu_load_report_clean(const RolltuiMenuLoadReport* r);

/* Parses a WHOLE menu file's TEXT into `out`, which the caller owns (a stack `RolltuiMenuItem`
 * default-constructed in C++, or `rolltui_menu_item_init`'d in C) — this FILLS it in place
 * rather than handing back a fresh allocation, releasing whatever `out` held first, so the one
 * node every file has costs the caller nothing beyond what it already owns.
 *
 * Returns 0 only when `text` is fundamentally unusable (a JSON syntax error, or the root is
 * not an object) — `report->error` says which, and `out` is left freshly empty. Otherwise 1,
 * even when the file's own shape is wrong (a bad kind, a duplicate id, a root that is not a
 * submenu, ...) — those are reported and `out` gets whatever the file was good for, matching
 * `menu_from_json`'s "still returns" contract. `report` is reset on every call. */
int rolltui_menu_parse_json(const char* text, size_t len, RolltuiMenuItem* out, RolltuiMenuLoadReport* report);
/* Serialises `root`, 2-space indented with a trailing newline (matches `json::dump(v, 2) +
 * "\n"`). REPLACES `*out`. */

#ifdef __cplusplus
inline RolltuiMenuActionList::~RolltuiMenuActionList() { rolltui_menu_action_list_release(this); }

#endif

/* ========================================================================================
 * transcript — the transcript widget
 * ======================================================================================== */
/* ---- options ------------------------------------------------------------------------------ */
/* `rolltui::TranscriptOptions` IS this struct. */
typedef struct RolltuiTranscriptOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int tab_width ROLLTUI_DEFAULT(8);
  int gap ROLLTUI_DEFAULT(1);   /* blank lines between entries */
  int inset ROLLTUI_DEFAULT(0); /* columns kept clear on each side of the area */
  int wheel_lines ROLLTUI_DEFAULT(3);
  int code_fold_over_lines ROLLTUI_DEFAULT(0); /* 0 disables, as in markdown's fold options */
  int code_cap_lines ROLLTUI_DEFAULT(0);
  unsigned long long multi_click_ms ROLLTUI_DEFAULT(400);
#ifdef __cplusplus
  bool operator==(const RolltuiTranscriptOptions&) const = default;
#endif
} RolltuiTranscriptOptions;

/* ---- one entry's cached layout ---------------------------------------------------------------- */
/* `rolltui::EntryLayout` IS this struct. It OWNS one span store: the markdown render puts the
 * entry's BODY lines at the front, and this layout's own drawn lines — the body behind the
 * entry's prefix, with the fold summary above them — are appended after and REFERENCE the
 * body's spans by index (Phase 15 m4). `body` is where the drawn ones begin. */
typedef struct RolltuiEntryLayout {
  RolltuiMdLines* store ROLLTUI_DEFAULT(nullptr); /* OWNED */
  size_t body ROLLTUI_DEFAULT(0);
  unsigned char folded ROLLTUI_DEFAULT(0);
  size_t hidden_lines ROLLTUI_DEFAULT(0); /* body lines a fold hides */

} RolltuiEntryLayout;

typedef struct RolltuiScrollAnchor {
  size_t entry ROLLTUI_DEFAULT(0); /* index into the document */
  size_t line ROLLTUI_DEFAULT(0);  /* line within that entry's block (gap lines first) */
  unsigned char follow ROLLTUI_DEFAULT(1);
} RolltuiScrollAnchor;

/* A position in the logical text: the grapheme starting at `offset` of `entry`'s text,
 * `length` bytes long (0 at the end of the text or for a boundary position). */
typedef struct RolltuiTextPos {
  size_t entry ROLLTUI_DEFAULT(0);
  size_t offset ROLLTUI_DEFAULT(0);
  size_t length ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  bool operator==(const RolltuiTextPos&) const = default;
#endif
} RolltuiTextPos;

/* Entry, then offset, then length — so of two positions at the same offset the one that
 * covers a grapheme is `last()`, and the range includes it. */
int rolltui_text_pos_less(const RolltuiTextPos* a, const RolltuiTextPos* b);

typedef struct RolltuiSelection {
  RolltuiTextPos anchor, head;
  unsigned char active ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  bool empty() const { return !active; }
  RolltuiTextPos first() const { return rolltui_text_pos_less(&head, &anchor) ? head : anchor; }
  RolltuiTextPos last() const { return rolltui_text_pos_less(&head, &anchor) ? anchor : head; }
  // The selected byte range within `entry`'s text of length `len`: [begin, end).
  bool range_in(std::size_t entry, std::size_t len, std::size_t& begin, std::size_t& end) const;
#endif
} RolltuiSelection;

/* One find hit, in the same logical space as a position. */
typedef struct RolltuiFindMatch {
  size_t entry ROLLTUI_DEFAULT(0);
  size_t offset ROLLTUI_DEFAULT(0);
  size_t length ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  bool operator==(const RolltuiFindMatch&) const = default;
#endif
} RolltuiFindMatch;

typedef struct RolltuiTranscriptStats {
  long layout_us ROLLTUI_DEFAULT(0);      /* the last layout() call */
  size_t entries_relaid ROLLTUI_DEFAULT(0);
  size_t total_lines ROLLTUI_DEFAULT(0);
  size_t cache_size ROLLTUI_DEFAULT(0);
} RolltuiTranscriptStats;

typedef struct RolltuiTranscript RolltuiTranscript;

/* THE HOST'S CLIPBOARD, as a function pointer and a context. NULL turns it off. */
void rolltui_transcript_set_copy(RolltuiTranscript* t, RolltuiCopyFn fn, void* ctx);



/* THE ELEVEN ACTION NAMES, handed over once. This file knows the RULES and none of the words. */
typedef struct RolltuiTranscriptActions {
  const char* line_up;
  const char* line_down;
  const char* page_up;
  const char* page_down;
  const char* top;
  const char* bottom;
  const char* find_next;
  const char* find_prev;
  const char* fold;
  const char* copy;
  const char* clear_selection;
} RolltuiTranscriptActions;

/* The LIBRARY'S OWN eleven (Phase 17 m2a). BORROWS static storage. */
const RolltuiTranscriptActions* rolltui_transcript_default_actions(void);

int rolltui_transcript_handle(RolltuiTranscript* t, const RolltuiEvent* e, const RolltuiDocument* doc,
                              unsigned long long now_ms, const RolltuiBindings* bindings,
                              const RolltuiTranscriptActions* actions);
/* True while a drag holds the pointer outside the area: tick at ~50 ms and re-layout. */
int rolltui_transcript_wants_tick(const RolltuiTranscript* t);
void rolltui_transcript_tick(RolltuiTranscript* t);

/* ---- scrolling --------------------------------------------------------------------------------------- */
void rolltui_transcript_scroll_page(RolltuiTranscript* t, int direction);
void rolltui_transcript_scroll_to_top(RolltuiTranscript* t);
void rolltui_transcript_scroll_to_bottom(RolltuiTranscript* t);
void rolltui_transcript_scroll(const RolltuiTranscript* t, RolltuiScrollAnchor* out);
size_t rolltui_transcript_total_lines(const RolltuiTranscript* t);
size_t rolltui_transcript_top_line(const RolltuiTranscript* t);

/* ---- folding ------------------------------------------------------------------------------------------ */
int rolltui_transcript_toggle_fold_nearest_top(RolltuiTranscript* t, const RolltuiDocument* doc);

/* ---- find -------------------------------------------------------------------------------------------- */
/* "" clears. Never scrolls: the reveal it asks for happens in layout(). 1 when it CHANGED. */
int rolltui_transcript_set_query(RolltuiTranscript* t, const char* q, size_t len);
const char* rolltui_transcript_query(const RolltuiTranscript* t, size_t* len);
size_t rolltui_transcript_match_count(const RolltuiTranscript* t);
/* The current match's 1-BASED position, for "3/17"; 0 when there is none. */
size_t rolltui_transcript_current_match_number(const RolltuiTranscript* t);
int rolltui_transcript_find_next(RolltuiTranscript* t);
int rolltui_transcript_find_prev(RolltuiTranscript* t);

/* ---- selection ----------------------------------------------------------------------------------------- */
/* The logical position under screen cell (x, y). 0 only for an empty document or a row above
 * the area. */
int rolltui_transcript_copy_selection(RolltuiTranscript* t);



/* ========================================================================================
 * layout_tree — the tree inside RolltuiLayout: a public type, so its lifecycle is public
 * ======================================================================================== */
#ifdef __cplusplus
namespace rolltui {
enum class Border : unsigned char { None, Single, Rounded, Double, Heavy };
enum class Anchor : unsigned char {
  TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight
};
// Declared, not defined: the vocabulary lives in `rolltui/Style.hpp`, and this file names
// no role — the m2 rule at `rolltui_diff.h`. An opaque enum declaration is a complete type
// because the underlying type is fixed, which is all a member needs.
enum class Role : unsigned char;

}  // namespace rolltui
#endif
/* floor(fraction * extent + 1e-6) + cells — the rule is Layout.hpp's. */
typedef struct RolltuiDim {
  double fraction ROLLTUI_DEFAULT(0);
  int cells ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  static constexpr RolltuiDim abs(int c) { return {0, c}; }
  static constexpr RolltuiDim rel(double f, int c = 0) { return {f, c}; }
  constexpr RolltuiDim operator+(const RolltuiDim& o) const { return {fraction + o.fraction, cells + o.cells}; }
  constexpr bool operator==(const RolltuiDim&) const = default;
#endif
} RolltuiDim;

/* `std::optional<Dim>` in C++ was four bytes of state the language supplied for free; here
 * it is a flag, which is the same thing said out loud. The C++ methods keep every existing
 * `if (p.min_w)` and `*p.min_w` compiling, so the port's diff stays about ownership. */
typedef struct RolltuiOptDim {
  unsigned char present ROLLTUI_DEFAULT(0);
  RolltuiDim d;
#ifdef __cplusplus
  RolltuiOptDim() = default;
  RolltuiOptDim(RolltuiDim v) : present(1), d(v) {}  // NOLINT(google-explicit-constructor)
  explicit operator bool() const { return present != 0; }
  bool has_value() const { return present != 0; }
  const RolltuiDim& operator*() const { return d; }
  const RolltuiDim& value() const { return d; }
  void reset() { present = 0; d = RolltuiDim{}; }
  RolltuiOptDim& operator=(RolltuiDim v) {
    present = 1;
    d = v;
    return *this;
  }
  constexpr bool operator==(const RolltuiOptDim& o) const {
    return present == o.present && (!present || d == o.d);
  }
#endif
} RolltuiOptDim;

/* How much of the parent split's axis a node takes. */
typedef struct RolltuiSplitSize {
  unsigned char fill ROLLTUI_DEFAULT(1); /* a share of the remainder, by `weight` */
  int weight ROLLTUI_DEFAULT(1);
  RolltuiDim dim; /* when !fill */
#ifdef __cplusplus
  static constexpr RolltuiSplitSize fixed(RolltuiDim d) { return {0, 1, d}; }
  static constexpr RolltuiSplitSize filling(int w = 1) { return {1, w, {}}; }
  constexpr bool operator==(const RolltuiSplitSize&) const = default;
#endif
} RolltuiSplitSize;

/* Where a popup layer sits on the screen; the base layer's is the whole of it. */
typedef struct RolltuiPlacement {
  RolltuiDim x, y;
  RolltuiDim w ROLLTUI_DEFAULT(RolltuiDim::rel(1)), h ROLLTUI_DEFAULT(RolltuiDim::rel(1));
#ifdef __cplusplus
  rolltui::Anchor anchor = rolltui::Anchor::TopLeft;
#else
  unsigned char anchor;
#endif
  unsigned char clamp ROLLTUI_DEFAULT(1);
  RolltuiOptDim min_w, min_h, max_w, max_h;
#ifdef __cplusplus
  bool operator==(const RolltuiPlacement&) const = default;
#endif
} RolltuiPlacement;

struct RolltuiLayoutNode;

/* A node's OWNED children. Pointers, not values, and that is the decision this whole file
 * exists to make visible: a child's address never moves, so a `const RolltuiLayoutNode*`
 * handed out by `place()` or `find()` stays valid across an edit. The C++ vector could not
 * promise that and nobody had noticed it was promising nothing. */
typedef struct RolltuiNodeList {
  struct RolltuiLayoutNode** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  // A pointer-of-pointers iterator that dereferences one level, so `for (const Node& c :
  // n.children)` still says what it said. Written twice rather than as a template because a
  // template at namespace scope may not appear inside `extern "C"`, and thirty lines here is
  // cheaper than moving the type out of the file its data lives in.
  struct iterator {
    RolltuiLayoutNode** p;
    RolltuiLayoutNode& operator*() const { return **p; }
    RolltuiLayoutNode* operator->() const { return *p; }
    iterator& operator++() {
      ++p;
      return *this;
    }
    iterator operator+(std::ptrdiff_t d) const { return {p + d}; }
    std::ptrdiff_t operator-(const iterator& o) const { return p - o.p; }
    bool operator==(const iterator& o) const { return p == o.p; }
  };
  struct const_iterator {
    RolltuiLayoutNode* const* p;
    const_iterator() : p(nullptr) {}
    const_iterator(RolltuiLayoutNode* const* q) : p(q) {}  // NOLINT(google-explicit-constructor)
    const_iterator(iterator i) : p(i.p) {}                 // NOLINT(google-explicit-constructor)
    const RolltuiLayoutNode& operator*() const { return **p; }
    const RolltuiLayoutNode* operator->() const { return *p; }
    const_iterator& operator++() {
      ++p;
      return *this;
    }
    const_iterator operator+(std::ptrdiff_t d) const { return {p + d}; }
    std::ptrdiff_t operator-(const const_iterator& o) const { return p - o.p; }
    bool operator==(const const_iterator& o) const { return p == o.p; }
  };
  using value_type = RolltuiLayoutNode;

  RolltuiNodeList() = default;
  RolltuiNodeList(const RolltuiNodeList&) = delete;  /* Phase 19 m2: copy is `rolltui_node_list_copy`, spelled */
  RolltuiNodeList(RolltuiNodeList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiNodeList& operator=(const RolltuiNodeList&) = delete;
  RolltuiNodeList& operator=(RolltuiNodeList&& o) noexcept;
  ~RolltuiNodeList();

  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  RolltuiLayoutNode& operator[](std::size_t i) { return *v[i]; }
  const RolltuiLayoutNode& operator[](std::size_t i) const { return *v[i]; }
  RolltuiLayoutNode& front() { return *v[0]; }
  const RolltuiLayoutNode& front() const { return *v[0]; }
  RolltuiLayoutNode& back() { return *v[n - 1]; }
  const RolltuiLayoutNode& back() const { return *v[n - 1]; }
  iterator begin() { return {v}; }
  iterator end() { return {v + n}; }
  const_iterator begin() const { return {v}; }
  const_iterator end() const { return {v + n}; }
  void push_back(RolltuiLayoutNode&& c);
  void push_back(const RolltuiLayoutNode& c);
  void insert(const_iterator at, RolltuiLayoutNode&& c);
  void erase(const_iterator at);
  void clear();
  bool operator==(const RolltuiNodeList& o) const;

 private:
#endif
} RolltuiNodeList;

/* A window (a content slot) or a split (a row/column of children). ONE struct for both,
 * exactly as the C++ had, because a split that could not be given a border and a title
 * would need a second one. */
typedef struct RolltuiLayoutNode {
#ifdef __cplusplus
  enum class Kind : unsigned char { Window = 0, Row, Column };
  Kind kind = Kind::Window;
#else
  unsigned char kind; /* 0 window, 1 row, 2 column */
#endif
  RolltuiStr id;      /* defaults to `content` for windows; optional on splits */
  RolltuiStr content; /* windows: "kind[:source]" */
#ifdef __cplusplus
  rolltui::Border border = rolltui::Border::None;
#else
  unsigned char border;
#endif
  RolltuiStr title;
  unsigned char focusable ROLLTUI_DEFAULT(0);
  unsigned char visible ROLLTUI_DEFAULT(1); /* hidden: takes no space, draws nothing */
#ifdef __cplusplus
  rolltui::Role background = static_cast<rolltui::Role>(ROLLTUI_ROLE_DEFAULT_BACKGROUND);
#else
  unsigned char background;
#endif
  RolltuiSplitSize size;
  RolltuiNodeList children;

#ifdef __cplusplus
  bool is_window() const { return kind == Kind::Window; }
  bool operator==(const RolltuiLayoutNode& o) const;
  RolltuiLayoutNode clone() const;
  // The builders the editor and the tests write trees with — C strings in, fields set
  // (Phase 19 m2). A container node takes its children by `children.push_back(std::move(c))`.
  static RolltuiLayoutNode window(const char* content, RolltuiSplitSize size = {});
  static RolltuiLayoutNode window_id(const char* id, const char* content, RolltuiSplitSize size = {});
  static RolltuiLayoutNode row(RolltuiSplitSize size = {});
  static RolltuiLayoutNode column(RolltuiSplitSize size = {});
#endif
} RolltuiLayoutNode;

/* Zeroes and defaults a node the caller owns the storage of. Every C caller starts here;
 * `RolltuiLayoutNode n = {0}` would give a window with `visible` 0 and no background, which
 * is exactly the kind of "the language answered a question nobody asked" this port removes. */
void rolltui_layout_node_init(RolltuiLayoutNode* n);
/* Releases everything BELOW and INSIDE `n`, leaving it zeroed. Does not free `n` itself —
 * a root lives in its layer, a child in the list that owns it. */
void rolltui_layout_node_release(RolltuiLayoutNode* n);
/* Deep copy: `to` is released first, then filled from `from`. */
void rolltui_layout_node_copy(RolltuiLayoutNode* to, const RolltuiLayoutNode* from);
/* Deep equality, including every child in order. */
int rolltui_layout_node_equal(const RolltuiLayoutNode* a, const RolltuiLayoutNode* b);

/* A node on the heap, initialised. The child lists own these. */
RolltuiLayoutNode* rolltui_layout_node_new(void);
/* Releases and frees. A no-op on NULL. */
void rolltui_layout_node_free(RolltuiLayoutNode* n);

/* Appends an EMPTY child and returns it — the C's `emplace_back`, so a caller never builds a
 * node on the stack and copies it in. */
RolltuiLayoutNode* rolltui_node_list_add(RolltuiNodeList* l);
void rolltui_node_list_insert(RolltuiNodeList* l, size_t i, RolltuiLayoutNode* n);
void rolltui_node_list_remove(RolltuiNodeList* l, size_t i); /* frees it */
void rolltui_node_list_clear(RolltuiNodeList* l);            /* frees every child */
void rolltui_node_list_release(RolltuiNodeList* l);          /* …and the array */
void rolltui_node_list_copy(RolltuiNodeList* to, const RolltuiNodeList* from);

typedef struct RolltuiLayer {
  RolltuiStr id; /* a popup's name in the layout file; "" for the base */
  RolltuiPlacement placement;
  RolltuiLayoutNode root;
  unsigned char modal ROLLTUI_DEFAULT(0);
  RolltuiStr focus; /* the focused window id; "" → first focusable in tree order */

#ifdef __cplusplus
  RolltuiLayer();
  // COPY IS DELETED (Phase 19 m2): `RolltuiLayer copy = *p;` was a deep copy here and a shallow
  // alias in C — the double-free Phase 16 m6 measured. The spelling is `clone()`, which is
  // `rolltui_layer_copy`. The destructor calls the named `rolltui_layer_release` and stays.
  RolltuiLayer(const RolltuiLayer&) = delete;
  RolltuiLayer(RolltuiLayer&& o) noexcept;
  RolltuiLayer& operator=(const RolltuiLayer&) = delete;
  RolltuiLayer& operator=(RolltuiLayer&& o) noexcept;
  ~RolltuiLayer();
  RolltuiLayer clone() const;
  bool operator==(const RolltuiLayer& o) const;
#endif
} RolltuiLayer;

void rolltui_layer_init(RolltuiLayer* l);
void rolltui_layer_release(RolltuiLayer* l);
void rolltui_layer_copy(RolltuiLayer* to, const RolltuiLayer* from);
/* Takes `from`'s buffers and leaves it empty — the move, written down for C. */
void rolltui_layer_move(RolltuiLayer* to, RolltuiLayer* from);
int rolltui_layer_equal(const RolltuiLayer* a, const RolltuiLayer* b);

/* ---- popups: an OWNED, growable array of Layer VALUES (Phase 17) --------------------------- */
/* `Layout::popups`' storage. A FLAT array of values, not individually-heap-boxed pointers like
 * RolltuiNodeList: nothing holds a `Layer*` across a mutation (a popup is always looked up by id
 * on demand — `Layout::popup()`), so there is no address-stability property worth paying an
 * extra indirection for. `RolltuiWindowStack`'s own `layers` array already proves the shape
 * safe: a Layer is trivially relocatable (every byte it owns is behind a pointer elsewhere), so
 * growing this array with `rolltui_grow_zeroed` and shifting on removal is exactly that same,
 * already-proven pattern one level up. */
typedef struct RolltuiLayerList {
  RolltuiLayer* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiLayerList() = default;
  RolltuiLayerList(const RolltuiLayerList&) = delete;  /* Phase 19 m2 */
  RolltuiLayerList(RolltuiLayerList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiLayerList& operator=(const RolltuiLayerList&) = delete;
  RolltuiLayerList& operator=(RolltuiLayerList&& o) noexcept;
  ~RolltuiLayerList();

  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  RolltuiLayer* data() { return v; }
  const RolltuiLayer* data() const { return v; }
  RolltuiLayer& operator[](std::size_t i) { return v[i]; }
  const RolltuiLayer& operator[](std::size_t i) const { return v[i]; }
  RolltuiLayer& back() { return v[n - 1]; }
  const RolltuiLayer& back() const { return v[n - 1]; }
  RolltuiLayer* begin() { return v; }
  RolltuiLayer* end() { return v + n; }
  const RolltuiLayer* begin() const { return v; }
  const RolltuiLayer* end() const { return v + n; }
  void push_back(RolltuiLayer&& l);
  // Removes the layer whose id matches (a no-op when none does) — Layout.hpp's
  // "remove this popup", the one mutation a host ever asks of this list by name.
  void erase_id(const char* id, std::size_t len);
  void clear();
  bool operator==(const RolltuiLayerList& o) const;

 private:
#endif
} RolltuiLayerList;

void rolltui_layer_list_release(RolltuiLayerList* l);          /* frees every layer + the array */
void rolltui_layer_list_clear(RolltuiLayerList* l);             /* frees every layer, keeps the array */
void rolltui_layer_list_copy(RolltuiLayerList* to, const RolltuiLayerList* from);
/* Appends an EMPTY layer and returns it — the C's `emplace_back`. */
RolltuiLayer* rolltui_layer_list_add(RolltuiLayerList* l);
void rolltui_layer_list_remove_id(RolltuiLayerList* l, const char* id, size_t len); /* no-op if absent */
int rolltui_layer_list_equal(const RolltuiLayerList* a, const RolltuiLayerList* b);

/* PLAIN DATA, and deliberately so: it is emitted per node per frame, it borrows the node it
 * describes, and it is what a host reads to draw. `node` is a BORROW valid as long as the
 * tree it came from is not edited. */
typedef struct RolltuiResolvedNode {
  const RolltuiLayoutNode* node ROLLTUI_DEFAULT(nullptr);
  RolltuiRect outer; /* the node's box before clipping to the frame */
  RolltuiRect inner; /* outer minus the border, clipped — what a split divides / a slot draws in */
  unsigned char focused ROLLTUI_DEFAULT(0);
  size_t layer ROLLTUI_DEFAULT(0);
} RolltuiResolvedNode;

#ifdef __cplusplus
/* ---- the C++ special members of the structs above (Phase 17 m3) ---------------------------
 * Each one is a CALLER of a C function declared above it, so "release this subtree" has one
 * implementation and a destructor reaches it rather than being a second mechanism.
 *
 * They were out-of-line in `rolltui/LayoutTree.cpp` until the C++ binding was deleted. They are not
 * part of that binding — they are what makes "the C++ type IS the C struct" true (Phase 14's
 * one-definition rule), so they had to keep a home; `inline`, beside the declarations they
 * implement, is that home and removes the last C++ translation unit from the library. */
inline RolltuiNodeList::~RolltuiNodeList() { rolltui_node_list_release(this); }

inline RolltuiNodeList& RolltuiNodeList::operator=(RolltuiNodeList&& o) noexcept {
  if (this != &o) {
    rolltui_node_list_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}

inline void RolltuiNodeList::push_back(RolltuiLayoutNode&& c) {
  RolltuiLayoutNode* p = rolltui_node_list_add(this);
  *p = std::move(c);
}

inline void RolltuiNodeList::push_back(const RolltuiLayoutNode& c) {
  RolltuiLayoutNode* p = rolltui_node_list_add(this);
  rolltui_layout_node_copy(p, &c);
}

inline void RolltuiNodeList::insert(const_iterator at, RolltuiLayoutNode&& c) {
  RolltuiLayoutNode* p = rolltui_layout_node_new();
  *p = std::move(c);
  rolltui_node_list_insert(this, static_cast<std::size_t>(at.p - v), p);
}

inline void RolltuiNodeList::erase(const_iterator at) {
  rolltui_node_list_remove(this, static_cast<std::size_t>(at.p - v));
}

inline void RolltuiNodeList::clear() { rolltui_node_list_clear(this); }

inline bool RolltuiNodeList::operator==(const RolltuiNodeList& o) const {
  if (n != o.n) return false;
  for (std::size_t i = 0; i < n; ++i)
    if (!rolltui_layout_node_equal(v[i], o.v[i])) return false;
  return true;
}

inline bool RolltuiLayoutNode::operator==(const RolltuiLayoutNode& o) const {
  return rolltui_layout_node_equal(this, &o) != 0;
}
inline RolltuiLayoutNode RolltuiLayoutNode::clone() const {
  RolltuiLayoutNode out;
  rolltui_layout_node_copy(&out, this);
  return out;
}
inline RolltuiLayoutNode RolltuiLayoutNode::window(const char* content, RolltuiSplitSize size) {
  RolltuiLayoutNode n;
  n.kind = Kind::Window;
  n.id = content;
  n.content = content;
  n.size = size;
  return n;
}
inline RolltuiLayoutNode RolltuiLayoutNode::window_id(const char* id, const char* content, RolltuiSplitSize size) {
  RolltuiLayoutNode n = window(content, size);
  n.id = id;
  return n;
}
inline RolltuiLayoutNode RolltuiLayoutNode::row(RolltuiSplitSize size) {
  RolltuiLayoutNode n;
  n.kind = Kind::Row;
  n.size = size;
  return n;
}
inline RolltuiLayoutNode RolltuiLayoutNode::column(RolltuiSplitSize size) {
  RolltuiLayoutNode n = row(size);
  n.kind = Kind::Column;
  return n;
}

inline RolltuiLayer::RolltuiLayer() {
  placement.w = RolltuiDim::rel(1);
  placement.h = RolltuiDim::rel(1);
  placement.clamp = 1;
}

inline RolltuiLayer::RolltuiLayer(RolltuiLayer&& o) noexcept
    : id(std::move(o.id)),
      placement(o.placement),
      root(std::move(o.root)),
      modal(o.modal),
      focus(std::move(o.focus)) {
  o.modal = 0;
}
inline RolltuiLayer RolltuiLayer::clone() const {
  RolltuiLayer out;
  rolltui_layer_copy(&out, this);
  return out;
}

inline RolltuiLayer& RolltuiLayer::operator=(RolltuiLayer&& o) noexcept {
  if (this != &o) {
    id = std::move(o.id);
    placement = o.placement;
    root = std::move(o.root);
    modal = o.modal;
    focus = std::move(o.focus);
    o.modal = 0;
  }
  return *this;
}

inline RolltuiLayer::~RolltuiLayer() = default;

inline bool RolltuiLayer::operator==(const RolltuiLayer& o) const { return rolltui_layer_equal(this, &o) != 0; }

inline RolltuiLayerList::~RolltuiLayerList() { rolltui_layer_list_release(this); }

inline RolltuiLayerList& RolltuiLayerList::operator=(RolltuiLayerList&& o) noexcept {
  if (this != &o) {
    rolltui_layer_list_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}

inline void RolltuiLayerList::push_back(RolltuiLayer&& l) { rolltui_layer_move(rolltui_layer_list_add(this), &l); }

inline void RolltuiLayerList::erase_id(const char* id, std::size_t len) { rolltui_layer_list_remove_id(this, id, len); }

inline void RolltuiLayerList::clear() { rolltui_layer_list_clear(this); }

inline bool RolltuiLayerList::operator==(const RolltuiLayerList& o) const { return rolltui_layer_list_equal(this, &o) != 0; }

#endif

/* ========================================================================================
 * layout — a host holds a RolltuiLayout and a window stack
 * ======================================================================================== */
/* The rule in Layout.hpp's header comment; absolute coordinates (parent.x/y added).
 *
 * INTO A CALLER'S RECT, NOT RETURNED — and the compiler is what said so. `RolltuiRect`
 * gained methods and default initializers the moment it became one definition, which stops
 * it being a C++98 POD, and Clang's `-Wreturn-type-c-linkage` then refuses to promise an ABI
 * for returning one from an `extern "C"` function. `rolltui_screen.h` wrote that rule down
 * for `RolltuiCell` in Phase 14 m2 and it applies here unchanged: suppressing the warning
 * would be asserting an ABI the compiler declines to promise. A rect PARAMETER by value is
 * fine — that has one answer every ABI agrees on for a trivially-copyable type. */
void rolltui_placement_resolve(const RolltuiPlacement* p, RolltuiRect parent, RolltuiRect* out);

/* ---- the text forms ---------------------------------------------------------------------------- */
/* "50%" | "100% - 32" | "25%+2" — NOT a bare "32". 1 on success.
 *
 * THESE CROSSED BECAUSE THE MENU NEEDED THEM (Phase 15 m5): a `size` or `dim` field checks a
 * keystroke as a prefix of a valid value and then canonicalises it, so the C menu widget has
 * to be able to parse and print a Dim. They are pure text, and putting them anywhere but
 * beside the type would have been a second definition of what a dim looks like. */
int rolltui_parse_dim(const char* text, size_t len, RolltuiDim* out);
/* "32" | "50%" | "100% - 32", into a caller's buffer. */
#define ROLLTUI_DIM_STRING_MAX 64
size_t rolltui_dim_to_string(RolltuiDim d, char* out, size_t cap);
/* The same, plus a bare integer as cells — a size as TYPED. */
int rolltui_parse_size_text(const char* text, size_t len, RolltuiSplitSize* out);
size_t rolltui_split_size_to_string(RolltuiSplitSize s, char* out, size_t cap);

/* Called once per node, in TREE ORDER (a container precedes its children). The caller
 * decides where they go — a vector, a filter, a single hit test. */
typedef void (*RolltuiResolvedSink)(void* ctx, const RolltuiResolvedNode* rn);

/* Lays out one tree inside `box` (a layer's resolved placement). Hidden nodes are omitted,
 * and a hidden ROOT emits nothing at all. */
void rolltui_resolve_tree(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                          RolltuiResolvedSink emit, void* ctx);

/* THE THREE ROLES A COMPOSE NEEDS, handed in as bytes. This file names none of them. */
typedef struct RolltuiLayoutRoles {
  unsigned char border;
  unsigned char border_active;
  unsigned char title;
  unsigned char overlay;
} RolltuiLayoutRoles;

/* THE LIBRARY'S OWN ANSWER, so a host does not have to invent one (Phase 17 m3).
 *
 * The four bytes above lived in `Layout.cpp`'s anonymous namespace, with the note *"handed
 * over as bytes; `rolltui/Style.hpp` is the one place these names exist"*. That note was
 * right while the library was C++ with a C core, and it is the EIGHTH instance of the rule
 * m2a's five were: `Layout.cpp` is deleted, the role names have been C since m2a
 * (`ROLLTUI_ROLE_LIST`), and every one of the three hosts calls
 * `rolltui_window_stack_compose` — so a table with no home does not disappear, it becomes
 * three hand-written copies. Found the way seven of the previous eight were: by converting a
 * consumer (`rolltui-paint`) and hitting the wall.
 *
 * BORROWS static storage, valid for the life of the process, never freed. A host that paints
 * its borders from other roles still passes its own struct; nothing became mandatory. */
const RolltuiLayoutRoles* rolltui_layout_default_roles(void);


/* The host fills a window's content slot into `rn->inner` (already clipped). NULL draws
 * nothing, which is what a golden-frame harness wants. */
typedef void (*RolltuiSlotFn)(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f);

/* WORKING MEMORY THE CALLER OWNS (CLAUDE.md strategy 3). Two screen-sized arm maps — the
 * joins written so far, and what was under the ring before this window cleared it — plus the
 * draw scratch a title needs. The C++ had the maps as `thread_local` in `compose_layer`,
 * which is the "nobody decided the scratch's lifetime" shape Phase 14's design lens names;
 * here the caller says how long they live. One buffer per ROLE, so the compose's maps and
 * the text walk's clusters cannot alias. */
typedef struct RolltuiComposeScratch RolltuiComposeScratch;
RolltuiComposeScratch* rolltui_compose_scratch_new(void);
void rolltui_compose_scratch_free(RolltuiComposeScratch* s);

#define ROLLTUI_SOURCE_REQUIRED 0
#define ROLLTUI_SOURCE_OPTIONAL 1
#define ROLLTUI_SOURCE_FORBIDDEN 2

/* The SHAPE of a source, when one is given — what the design editor's source field accepts. */
#define ROLLTUI_SOURCE_SHAPE_NAME 0 /* a bound name: a document, a row source, a menu file, a key scope */
#define ROLLTUI_SOURCE_SHAPE_TEXT 1 /* free text, may be empty: a literal (`text:`), a path (`file:`) */

/* Which rung answered, which is the whole of what the C decides. */
#define ROLLTUI_KIND_UNKNOWN 0  /* neither rung */
#define ROLLTUI_KIND_LIBRARY 1  /* rung 1: `*row < rolltui_widget_kind_library_count()` */
#define ROLLTUI_KIND_HOST 2     /* rung 2: `*row` is at or past that boundary */

/* Resolves a kind NAME. `*row` is its row in the one enumeration, filled for BOTH answering
 * rungs; `rule` and `source_is` (a BORROW valid until the registry changes) likewise. Any
 * out-param may be NULL. */
int rolltui_widget_kind_resolve(const char* name, size_t len, size_t* row, unsigned char* rule,
                                const char** source_is, size_t* source_is_len);
/* The one enumeration. Rows [0, library_count) are the library's closed table, in table order;
 * rows [library_count, count) are a host's, in registration order. A row past the end reads as
 * "" / REQUIRED / NAME rather than past either table. */
size_t rolltui_widget_kind_count(void);
size_t rolltui_widget_kind_library_count(void);
const char* rolltui_widget_kind_name(size_t row, size_t* len);
unsigned char rolltui_widget_kind_source_shape(size_t row);

/* Why a registration was refused, so the shim can say it in words. 0 is accepted. */
#define ROLLTUI_REGISTER_OK 0
#define ROLLTUI_REGISTER_EMPTY 1
#define ROLLTUI_REGISTER_HAS_COLON 2
#define ROLLTUI_REGISTER_IS_LIBRARY 3    /* rung 1 is never shadowed */
#define ROLLTUI_REGISTER_RULE_DIFFERS 4  /* already registered, with another source rule */
int rolltui_widget_kind_register(const char* name, size_t len, unsigned char rule, const char* source_is,
                                 size_t source_is_len);

/* PLAIN DATA: the kind's NAME and its source — the two halves of `kind[:source]` and nothing
 * more. Until Phase 18 m2 this carried a C++-only `WidgetKind` enum plus a `registered_name`
 * that was empty for every library kind: two fields to say one name. Every member has correct
 * value semantics on its own (`RolltuiStr`'s), so — exactly like `RolltuiLayoutNode` one level
 * up — this type declares NO constructor, destructor or assignment of its own, and `operator==`
 * needs only `= default`. An EMPTY `kind` names nothing: `rolltui_content_parse` fills one and
 * `rolltui_widget_kind_resolve` answers for one; neither invents a default kind. */
typedef struct RolltuiContent {
  RolltuiStr kind;   /* the kind's name — its identity, whichever rung it resolves at */
  RolltuiStr source; /* the part after the first ':' — a bound name, a literal, a path */
#ifdef __cplusplus
  bool operator==(const RolltuiContent&) const = default;
#endif
} RolltuiContent;


#define ROLLTUI_CONTENT_PROBLEM_NONE 0
#define ROLLTUI_CONTENT_PROBLEM_UNKNOWN_KIND 1
#define ROLLTUI_CONTENT_PROBLEM_MISSING_SOURCE 2
#define ROLLTUI_CONTENT_PROBLEM_FORBIDDEN_SOURCE 3

/* Parses "kind[:source]". 1 on success: `*row` is the kind's row in the registry (both rungs)
 * and `*is_host` says which rung answered (0 library, 1 host). `name`/`name_len` (the part
 * before the colon) and `source`/`source_len` (the part after, "" with a valid pointer when
 * there was none) are always filled and are BORROWS into `text` — never a copy, because a
 * caller that wants its own string is about to make one anyway (`RolltuiContent`'s shape). On
 * failure (0): `problem` says which of the three ways (never None), and `why` — cleared on
 * entry — gets the exact sentence `rolltui::parse_content` always produced. */
int rolltui_content_parse(const char* text, size_t len, size_t* row, int* is_host, const char** name,
                         size_t* name_len, const char** source, size_t* source_len, unsigned char* problem,
                         RolltuiStr* why);
/* content_to_string's join rule: `kind_name`, then ":" + `source` exactly when `rule` says
 * the colon belongs (Required always; Optional only when `source` is non-empty). REPLACES
 * `*out`. */
void rolltui_content_format(const char* kind_name, size_t kind_name_len, const char* source, size_t source_len,
                            unsigned char rule, RolltuiStr* out);

const char* rolltui_anchor_name(unsigned char a, size_t* len); /* "" when `a` is out of range */
int rolltui_anchor_from_name(const char* name, size_t len, unsigned char* out);
const char* rolltui_border_name(unsigned char b, size_t* len);
int rolltui_border_from_name(const char* name, size_t len, unsigned char* out);

/* Role names: ask back rather than carry a table (see above). `role_from_name` returns 1
 * and fills `*out` on a recognised name; `role_name` returns the name's length, writing at
 * most `cap` bytes plus a NUL into `out` (0/nothing written when the ordinal is unknown to
 * the caller) — the same "caller-owned buffer, BORROW the count back" shape `rolltui_chord_
 * to_string` already uses, chosen over a `const char**` BORROW because a role's name has no
 * storage on this side of the call to borrow FROM. */
typedef int (*RolltuiRoleFromNameFn)(void* ctx, const char* name, size_t len, unsigned char* out);
typedef size_t (*RolltuiRoleNameFn)(void* ctx, unsigned char role, char* out, size_t cap);
#define ROLLTUI_ROLE_NAME_MAX 32 /* longest shipped role name plus room; Style.hpp's table is the oracle */

/* Bundled rather than three flat parameters threaded through every loader/dumper call: one
 * thing to hand over, and `rolltui_action_decl_problem` needs only the scope half of it. */
typedef struct RolltuiLayoutHooks {
  RolltuiScopeFn is_library_scope;
  void* scope_ctx;
  RolltuiRoleFromNameFn role_from_name;
  void* role_from_name_ctx;
  RolltuiRoleNameFn role_name;
  void* role_name_ctx;
} RolltuiLayoutHooks;

/* THE LIBRARY'S OWN, and the reason this exists is the reason the hooks themselves are now
 * vestigial (Phase 17 m2a). The three callbacks were invented because the role names and the
 * library's scope list were C++ facts a C file could not reach — the comment above still says
 * "ask back rather than carry a table". Both are C now (`rolltui_role_from_name`/`_name` in
 * rolltui_style.h, `rolltui_bindings_library_scope` one header over), so the library can
 * answer its own questions and every caller that was writing this table by hand can stop.
 *
 * Two were writing it by hand: `Layout.cpp`'s private `kHooks`, and a VERBATIM copy in
 * `layout_test.cpp` whose own comment justified itself — *"copied because they are not
 * exported (by design: the algorithm is the boundary's, the shim's OWN plumbing is not part
 * of its public surface either)"*. Correct while the shim existed; wrong once the shim is what
 * is being deleted, which is the fifth time this phase that same sentence has had to be
 * reversed. Every host in m3 would have been the third, fourth and fifth copy.
 *
 * The parameter STAYS on every function below rather than being removed: a host with its own
 * role vocabulary is exactly what the hooks were for, and that case is real (an app profile's
 * kinds). This is the default, not a policy. BORROWS static storage. */
const RolltuiLayoutHooks* rolltui_layout_default_hooks(void);

/* Why `name` cannot be declared as an action ("" when it can) — Layout.hpp's three rules, as
 * ONE function, so the loader and the design editor refuse exactly the same names with
 * exactly the same words. REPLACES `*out`. Calls back through `hooks->is_library_scope` for
 * "which scopes are the library's" — that stays Bindings' vocabulary (rolltui_bindings.h's
 * own rule), never duplicated here. */
void rolltui_action_decl_problem(const char* name, size_t len, const RolltuiLayoutHooks* hooks, RolltuiStr* out);

/* ---- the report: unknown keys / bad values are problems, notes are not (Layout.hpp's
 * `LayoutLoadReport::clean()`). Transparent, the same shape `rolltui_app_profile.h`'s own
 * report uses: `RolltuiStr` values in GROWING AMORTISED arrays. Zero-initialise before use. */
typedef struct RolltuiLayoutReport {
  RolltuiStr error; /* non-empty: the file was unusable */
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
  /* Things the loader DID that the file did not ask for and a host may want to say once —
   * today exactly one: a file declaring no "actions" is given the shipped default's. NOT part
   * of "clean", because none of it is a problem.
   *
   * It was called `migrated` until 2026-09-05, when the Phase 9 content-name rewrite that was
   * its first feeder was retired. The name was the FIRST CALLER's, not the field's, and the
   * surviving caller is not a migration at all — so the field is named for what it holds. */
  RolltuiStr* notes;
  size_t notes_n, notes_cap;
} RolltuiLayoutReport;

void rolltui_layout_report_release(RolltuiLayoutReport* r); /* frees everything; zeroes it */
int rolltui_layout_report_clean(const RolltuiLayoutReport* r);

/* One entry of a layout file's "actions" object: a name and its English description. Named
 * apart from Bindings' own `ActionDecl` (which this file does not include, keeping the
 * layering the header comment above states: a layout FILE's vocabulary must not depend on
 * Bindings' C++-only vocabulary) — this is the layout FILE's own two strings, "the actions
 * THIS SCREEN emits" (Layout.hpp). Phase 17: it is ALSO now `RolltuiLayout::actions`' own
 * element type (below), not only the loader's transient one — the two structs holding the
 * same two strings was a fact about the file format before it was a shared type, and now it
 * is both without this file ever naming `rolltui::ActionDecl`; the shim (`Layout.cpp`)
 * converts to that std::string-based type at the one seam a handful of unported hosts still
 * need it (`action_decls()`). */
typedef struct RolltuiLayoutAction {
  RolltuiStr name;
  RolltuiStr description;
} RolltuiLayoutAction;

/* An OWNED, growable array of `RolltuiLayoutAction` VALUES — `RolltuiLayout::actions`' storage.
 * A flat array, the same shape as `RolltuiLoadedLayout::actions` below and for the same
 * reason: nothing holds an `Action*` across a mutation (a caller reads one, or appends, or
 * removes by index), so there is no address-stability property worth an extra indirection
 * for, and each element is two `RolltuiStr`s — already trivially relocatable. */
typedef struct RolltuiActionList {
  RolltuiLayoutAction* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiActionList() = default;
  RolltuiActionList(const RolltuiActionList&) = delete;  /* Phase 19 m2: copy is `rolltui_action_list_copy`, spelled */
  RolltuiActionList(RolltuiActionList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiActionList& operator=(const RolltuiActionList&) = delete;
  RolltuiActionList& operator=(RolltuiActionList&& o) noexcept;
  ~RolltuiActionList();

  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  RolltuiLayoutAction* data() { return v; }
  const RolltuiLayoutAction* data() const { return v; }
  RolltuiLayoutAction& operator[](std::size_t i) { return v[i]; }
  const RolltuiLayoutAction& operator[](std::size_t i) const { return v[i]; }
  RolltuiLayoutAction& back() { return v[n - 1]; }
  const RolltuiLayoutAction& back() const { return v[n - 1]; }
  RolltuiLayoutAction* begin() { return v; }
  RolltuiLayoutAction* end() { return v + n; }
  const RolltuiLayoutAction* begin() const { return v; }
  const RolltuiLayoutAction* end() const { return v + n; }
  void push_back(const RolltuiLayoutAction& a);
  // Removes the action named `name` (a no-op when none is) — the editor's "remove this
  // action", the one mutation a host ever asks of this list by name rather than by index.
  void erase_name(const char* name, std::size_t len);
  void clear();
  bool operator==(const RolltuiActionList& o) const;

 private:
#endif
} RolltuiActionList;

void rolltui_action_list_release(RolltuiActionList* l);
void rolltui_action_list_clear(RolltuiActionList* l);
void rolltui_action_list_copy(RolltuiActionList* to, const RolltuiActionList* from);
/* Appends an EMPTY action and returns it — the C's `emplace_back`. */
RolltuiLayoutAction* rolltui_action_list_add(RolltuiActionList* l);
void rolltui_action_list_remove_name(RolltuiActionList* l, const char* name, size_t len); /* no-op if absent */
int rolltui_action_list_equal(const RolltuiActionList* a, const RolltuiActionList* b);

/* The parsed layout: a TRANSIENT carrier, never retained past one load. Phase 17 gave
 * `rolltui::Layout` this same shape (`RolltuiLayout` below shares `RolltuiStr name` and
 * `RolltuiActionList actions` with it byte-for-byte), so the reason this stays a SEPARATE
 * struct is no longer "the fields cannot be shared" — it is that this one is scoped to a
 * single `rolltui_load_layout*` call and the loader's own bookkeeping (`actions_cap` growing
 * across a parse that has not decided the file is even usable yet) has no business being
 * `RolltuiLayout`'s API. The shim converts once, right after a load (`Layout.cpp`'s
 * `loaded_to_layout`): `base`/`popups` already ARE `RolltuiLayer`/`RolltuiLayer*`, so that
 * conversion MOVES rather than copies a tree it is about to release anyway. */
typedef struct RolltuiLoadedLayout {
  RolltuiStr name;
  int min_width, min_height;
  RolltuiLayoutAction* actions;
  size_t actions_n, actions_cap;
  RolltuiLayer base;
  RolltuiLayer* popups;
  size_t popups_n, popups_cap;
} RolltuiLoadedLayout;

/* ---- the layout itself: the ENDURING value a host holds (Phase 17) -------------------------
 *
 * `rolltui::Layout` IS this struct — the same one-definition rule as `Node`/`Layer`/`Dim`.
 * Every member already has correct value semantics on its own (`RolltuiStr`, the two lists
 * above, `RolltuiLayer`), so — exactly like `RolltuiLayoutNode` and unlike the two OWNING
 * ARRAYS above it — this type declares no constructor, destructor or copy/move of its own;
 * the compiler-generated ones already do the right thing by recursively using each member's.
 * `popup()` is the one convenience worth a member function (a host reaches for it by name at
 * ~a dozen call sites): a linear scan needs nothing this header does not already have. */
typedef struct RolltuiLayout {
  RolltuiStr name;
  int min_width ROLLTUI_DEFAULT(0);
  int min_height ROLLTUI_DEFAULT(0);
  RolltuiActionList actions; /* the actions this screen emits, in file order */
  RolltuiLayer base;
  RolltuiLayerList popups; /* declared placements a host pushes by id */

#ifdef __cplusplus
  const RolltuiLayer* popup(const char* id, std::size_t len) const {
    for (std::size_t i = 0; i < popups.size(); ++i)
      if (popups[i].id.eq(id, len)) return &popups[i];
    return nullptr;
  }
  bool operator==(const RolltuiLayout&) const = default;
  // The explicit copy (Phase 19 m2): `rolltui_layout_copy`, spelled at the call site. Copying
  // by `=` is deleted through every member, which is the point.
  RolltuiLayout clone() const;
#endif
} RolltuiLayout;

void rolltui_layout_init(RolltuiLayout* l);    /* zeroes; inits `base` */
void rolltui_layout_release(RolltuiLayout* l); /* frees name/actions/base/popups; zeroes */
void rolltui_layout_copy(RolltuiLayout* to, const RolltuiLayout* from);
/* The C-callable form of `RolltuiLayout::popup()`, for a pure C caller. */
const RolltuiLayer* rolltui_layout_popup(const RolltuiLayout* l, const char* id, size_t len);

void rolltui_loaded_layout_init(RolltuiLoadedLayout* l);    /* zeroes; inits `base` */
void rolltui_loaded_layout_release(RolltuiLoadedLayout* l); /* frees name/actions/base/popups; zeroes */

/* Unpacks a filled `RolltuiLoadedLayout` into a fresh `RolltuiLayout`, ONCE, right after a
 * load — the loaded carrier is never retained past this call (rolltui_load_layout[_text]'s
 * contract). `out` is a caller-owned `RolltuiLayout` this fills (run `rolltui_layout_init`
 * on it first, or hand in a freshly zeroed one); its previous contents, if any, are NOT
 * released first. MOVES `name`, `actions` and `popups` (whole-array field adoption — the two
 * structs share `name`/`actions`(list)/`base`/`popups`(list) byte for byte, this file's own
 * comment on `RolltuiLayout` states why) and `base` (`rolltui_layer_move`); `loaded` is left
 * with empty actions/popups/base and must still be released by the caller (its `name` is
 * untouched by the move above and would otherwise leak).
 *
 * THIS IS THE ONE HOME for a conversion that existed twice before it: `rolltui::Layout.cpp`'s
 * `loaded_to_layout` (anonymous-namespace-private, one push_back per action/popup) and
 * `rolltui_presets.c`'s `loaded_layout_move` (file-static, this same field-adopt shape). Both
 * predate this accessor; this is the version to reach for from anywhere else, including a
 * pure-C caller, which neither of those was. */
void rolltui_loaded_layout_to_layout(RolltuiLoadedLayout* loaded, RolltuiLayout* out);


/* ---- THE SHIPPED SCREEN'S OWN ACTIONS (Phase 17) ----------------------------------------
 * The "actions" object of the embedded `default` layout, parsed ONCE and cached for the life
 * of the process (released by `rolltui_shutdown`). This is the fallback a file that declares
 * no actions of its own gets, and it is what `rolltui_bindings_default` validates the shipped
 * key file against.
 *
 * It moved out of C++ because it is BEHAVIOUR, not a wrapper: the primitives below
 * (`rolltui_layout_read_actions_key`) were already here, but the parse-once-and-cache around
 * them lived only in `rolltui::shipped_default_actions()`, so a pure-C host had to re-derive
 * it — the duplicate-implementation failure this library keeps finding one level down.
 *
 * BORROWS: the array is the library's and is valid until `rolltui_shutdown`. Never freed by
 * the caller. `*n` is the count; the array is NULL only if the embedded file is unparseable,
 * which is a build mistake rather than a runtime one. */
const RolltuiLayoutAction* rolltui_layout_shipped_default_actions(size_t* n);

/* The embedded layout file of that name, as TEXT ("" when there is none). One definition site
 * for "which file is `default`", so the actions above and a host loading the same screen do
 * not each scan the embedded table their own way. */
const char* rolltui_layout_builtin_json(const char* name, size_t len, size_t* out_len);


/* The same over TEXT: parses it first, and a JSON syntax error also becomes `report->error`
 * (0 returned) rather than reaching the loader at all. */
int rolltui_load_layout_text(const char* text, size_t len, RolltuiLoadedLayout* out,
                             const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                             const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report);


typedef struct RolltuiWindowStack RolltuiWindowStack;

/* A stack with one empty base layer, which is what `WindowStack{}` has always meant. */
RolltuiWindowStack* rolltui_window_stack_new(void);
void rolltui_window_stack_free(RolltuiWindowStack* s);

/* Replaces the base layer by COPY. Popup layers stay; the base's focus id is kept when a
 * window with that id still exists. */
void rolltui_window_stack_set_base(RolltuiWindowStack* s, const RolltuiLayer* base);
RolltuiLayer* rolltui_window_stack_base(RolltuiWindowStack* s);
/* Takes `popup` BY MOVE and leaves the caller's empty — the ownership `push(Layer)` was
 * doing twice by value. */
void rolltui_window_stack_push(RolltuiWindowStack* s, RolltuiLayer* popup);

/* PUSHES A POPUP THE LAYOUT DECLARED, BY ID — deep-copies it and pushes the copy. 1 when the
 * layout declares one of that id, 0 when it does not (nothing is pushed). A layer a HOST built
 * itself still goes through `_push` above; this is only the by-id case.
 *
 * ADDED Phase 16 m6, and it is rule 5's tell firing for the fourth time
 * (`rolltui/rolltui.h`): two hosts had hand-written this at SIX call sites — and in two
 * different spellings, which is what makes it worse than the usual duplication. `studio.cpp`
 * wrote three of them as
 *
 *     RolltuiLayer copy = *p;  rolltui_window_stack_push(stack, &copy);
 *
 * **and that line is a different operation in the two languages.** Under `__cplusplus` it runs
 * `RolltuiLayer`'s copy constructor, which is a DEEP copy through `rolltui_layer_copy`. In C it
 * is a shallow struct assignment: the copy aliases the layout's own `RolltuiStr` buffers and
 * child arrays, `_push` moves those pointers into the stack, and freeing the stack then frees
 * storage the layout still holds. Measured, not reasoned — a five-line pure-C program doing
 * exactly the studio's line aborts on `AddressSanitizer: attempting double-free` inside
 * `rolltui_shutdown`'s release of the built-in layout cache.
 *
 * So the C++ special members were ABSORBING a missing operation — Phase 17 m5's own verdict,
 * one level further out than the binding it was written about: you cannot see a wall from
 * behind it, and until `c_consumer_test.c` existed nothing in this repository stood in front
 * of this one. The fix is the API, never the wrapper. */
int rolltui_window_stack_push_popup(RolltuiWindowStack* s, const RolltuiLayout* layout, const char* id, size_t len);
int rolltui_window_stack_pop(RolltuiWindowStack* s); /* 0 when only the base remains */
size_t rolltui_window_stack_depth(const RolltuiWindowStack* s);
const RolltuiLayer* rolltui_window_stack_layer(const RolltuiWindowStack* s, size_t i);
int rolltui_window_stack_has_popup(const RolltuiWindowStack* s, const char* id, size_t len);


const RolltuiLayoutNode* rolltui_window_stack_focused(const RolltuiWindowStack* s);
void rolltui_window_stack_focus(RolltuiWindowStack* s, const char* id, size_t len);

/* Every layer resolved against `screen`, in draw order, `focused` set on the one focused
 * window. */
void rolltui_window_stack_resolve(const RolltuiWindowStack* s, RolltuiRect screen, RolltuiResolvedSink emit,
                                  void* ctx);
void rolltui_window_stack_compose(const RolltuiWindowStack* s, RolltuiFrame* f, RolltuiRect screen,
                                  const RolltuiStyle* styles, const RolltuiLayoutRoles* roles,
                                  RolltuiSlotFn render, void* ctx, int ambiguous_wide,
                                  RolltuiComposeScratch* scratch);

/* THE THREE ACTION NAMES, handed in — this file knows the RULES and none of the words. */
typedef struct RolltuiStackActions {
  const char* close_popup;
  const char* focus_next;
  const char* focus_prev;
} RolltuiStackActions;

/* The library's own three, expanded from the SAME closed list the other four per-widget
 * tables come from (`rolltui_library_actions.c`) — the fifth expansion of one vocabulary,
 * not a fifth spelling of it. Same reason as `rolltui_layout_default_roles` above: the words
 * were `Layout.cpp`'s `kStackActions` and all three hosts call `rolltui_window_stack_route`.
 * BORROWS static storage; a host with its own words still passes its own struct. */
const RolltuiStackActions* rolltui_stack_default_actions(void);

#define ROLLTUI_ROUTE_DELIVER 0
#define ROLLTUI_ROUTE_CLOSED_POPUP 1
#define ROLLTUI_ROUTE_FOCUS_MOVED 2
#define ROLLTUI_ROUTE_DROPPED 3

/* Routes one event. `window` is filled with the target id — a COPY, because the
 * ClosedPopup case names a layer this call has already freed. */
unsigned char rolltui_window_stack_route(RolltuiWindowStack* s, const RolltuiEvent* e, RolltuiRect screen,
                                         const RolltuiBindings* bindings, const RolltuiStackActions* actions,
                                         RolltuiStr* window);
/* The window a press captured the pointer for, until its release ("" when none). */

#ifdef __cplusplus
/* ---- `RolltuiActionList`'s C++ special members (Phase 17 m3) --------------------------------
 * Each is a CALLER of a C function declared above, so "release this list" has exactly one
 * implementation. They were out-of-line in `rolltui/Layout.cpp` for a stated reason that still
 * holds and that this placement satisfies: an inline body INSIDE the struct is parsed in a
 * complete-class context for member names, but an ordinary name like
 * `rolltui_action_list_release` must already be declared at that point — and it is declared
 * AFTER the struct. Down here, after every declaration, that is no longer true of anything.
 *
 * They are not part of the deleted binding: they are what makes "the C++ type IS the C struct"
 * true (Phase 14's one-definition rule). */
inline RolltuiActionList::~RolltuiActionList() { rolltui_action_list_release(this); }
inline RolltuiLayout RolltuiLayout::clone() const {
  RolltuiLayout out;
  rolltui_layout_copy(&out, this);
  return out;
}

inline RolltuiActionList& RolltuiActionList::operator=(RolltuiActionList&& o) noexcept {
  if (this != &o) {
    rolltui_action_list_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}

inline void RolltuiActionList::push_back(const RolltuiLayoutAction& a) {
  RolltuiLayoutAction* p = rolltui_action_list_add(this);
  rolltui_str_set(&p->name, a.name.p, a.name.n);
  rolltui_str_set(&p->description, a.description.p, a.description.n);
}

inline void RolltuiActionList::erase_name(const char* name, std::size_t len) {
  rolltui_action_list_remove_name(this, name, len);
}

inline void RolltuiActionList::clear() { rolltui_action_list_clear(this); }

inline bool RolltuiActionList::operator==(const RolltuiActionList& o) const {
  return rolltui_action_list_equal(this, &o) != 0;
}

#endif

/* ========================================================================================
 * widgets — the window registry and the plugin-facing half a host kind reaches its sources through
 * ======================================================================================== */
#ifdef __cplusplus
namespace rolltui {
enum class EffectState : unsigned char;

}  // namespace rolltui
#endif
#define ROLLTUI_AXIS_VERTICAL 0
#define ROLLTUI_AXIS_HORIZONTAL 1

typedef struct RolltuiScrollExtent {
  size_t first ROLLTUI_DEFAULT(0);   /* the first visible line */
  size_t visible ROLLTUI_DEFAULT(0); /* how many lines the viewport shows */
  size_t total ROLLTUI_DEFAULT(0);   /* how many there are */
} RolltuiScrollExtent;

/* The rect a widget draws in: the window's inner rect, less one column on each side when it
 * is bordered — the widget owns the breathing room inside the frame. */
void rolltui_content_rect(const RolltuiResolvedNode* rn, RolltuiRect* out);

typedef struct RolltuiWidgetPlugin {
  /* ---- REQUIRED (rule 2) ---- */
  /* Frees `ctx`. The window table calls this and nothing else ever does. */
  void (*destroy)(void* ctx);
  void (*layout)(void* ctx, const RolltuiResolvedNode* rn);
  void (*draw)(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f);

  /* ---- OPTIONAL: NULL means the behaviour stated on the line (rule 2) ---- */

  /* Why this widget cannot draw, into `out`; 0 when it can. NULL: it always can. */
  int (*problem)(void* ctx, RolltuiStr* out);
  /* Note `i`, into `out`; 0 when there is no i-th note. A note does NOT stop the widget
   * drawing — a menu file's unknown key is named in the report and the menu still shows.
   * NULL: no notes. */
  int (*note_at)(void* ctx, size_t i, RolltuiStr* out);
  /* The outer extent this widget wants along its parent's axis, into `out`; 0 to let the
   * layout decide. NULL: the layout decides, which is every widget but the input. */
  int (*desired_outer)(void* ctx, int inner_w, int parent_extent, int border, int* out);
  /* 1 when the event was consumed. NULL: nothing is consumed — the kinds whose whole
   * interaction is scrolling implement this; a host drives an input or a menu itself. */
  int (*handle)(void* ctx, const RolltuiEvent* e);
  /* REPORTS its extent, so the window may draw a bar; 0 for none. NULL: no bar. */
  int (*scroll_extent)(void* ctx, unsigned char axis, RolltuiScrollExtent* out);
  /* ACCEPTS a new first line, so the bar may be dragged; 0 to decline being driven. Anything
   * that returns 1 must CLAMP. NULL: reports but will not be driven (rule 4). */
  int (*scroll_to)(void* ctx, unsigned char axis, size_t first);
} RolltuiWidgetPlugin;

/* One widget: what it IS and how to talk to it. The plugin is a BORROW of a table the
 * implementor keeps (a `static const` per kind); `ctx` is OWNED by whoever holds this. */
typedef struct RolltuiWidget {
  const RolltuiWidgetPlugin* vt;
  void* ctx;
} RolltuiWidget;

typedef struct RolltuiWindows RolltuiWindows;

/* Builds a widget for `content` (the whole string, "kind:source"). Returns a widget whose
 * `ctx` the table then OWNS, or a zeroed one to mean "I cannot build this" — which is not an
 * error path: `Windows` draws the error panel and names it in the report. */
typedef RolltuiWidget (*RolltuiWidgetFactory)(void* ctx, const char* content, size_t len);

RolltuiWindows* rolltui_windows_new(void);
void rolltui_windows_free(RolltuiWindows* w);

/* Registers a kind NAME with the factory that builds it. The library's own seven go through
 * this call at construction, exactly as a host's does (rule 5) — with `free_ctx` NULL, since
 * their `ctx` is the `Windows` object itself and is not this table's to release. A host's own
 * kind passes a real `free_ctx`, which runs when the row is replaced (a second `register_kind`
 * for the same name) and at `rolltui_windows_free` — the same shape `rolltui_effect_register`
 * uses for a host's effect kinds. The layout vocabulary's half of the registration — and the
 * refusal to shadow a library kind — is `rolltui_widget_kind_register` in `rolltui_layout.h`. */
void rolltui_windows_register_kind(RolltuiWindows* w, const char* name, size_t len,
                                   RolltuiWidgetFactory factory, void* ctx, void (*free_ctx)(void*));

/* The widget for `content`, created on demand and never destroyed until this `Windows` is
 * (Widgets.hpp: two windows on one content are two views of one widget). */
RolltuiWidget* rolltui_windows_widget_for(RolltuiWindows* w, const char* content, size_t len);
/* The widget a WINDOW holds, or NULL — filled by `sync`. */

/* ---- THE TYPED WIDGETS, OWNED HERE AND REACHABLE BY NAME (Phase 17 m1c, 2026-09-05) ------
 * `rolltui_windows_at`/`_widget_for` above hand back an opaque `RolltuiWidget{vt, ctx}`:
 * enough to DRAW a widget and route an event at it, and nothing else. TWO CONSUMERS reached
 * for the missing typed call on the same day, separately (roll's `TuiFrontend` and
 * `layout_test`) — `rolltui.h` rule 5's tell that the API was wrong rather than the consumers.
 *
 * THE FIX IS OWNERSHIP, NOT AN ACCESSOR, and that distinction is why this took two attempts.
 * The three accessors alone were written once and REVERTED: `Windows` kept these widgets in
 * C++ maps (`Widgets.cpp`'s `inputs_`/`transcripts_`/`menus_`) that the built-in factories
 * constructed from, so a C-side map would have been a SECOND owner — a host asking for
 * `input:x` would get one object and the window drawing `input:x` would draw another, two
 * views of one source silently diverging, and every suite passes either way. The maps moved
 * HERE and the three built-in factories were repointed at them in ONE change; these accessors
 * and those factories now read the same table, so there is one object per source by
 * construction.
 *
 * Created on demand and never destroyed until `w` is — the same rule the widget table itself
 * states ("two windows on one content are two views of one widget"). Every one comes back
 * fully formed: an input with the library's defaults, a transcript with the library's roles
 * and whatever highlighter `rolltui_windows_set_highlight` last set, a menu with its own
 * single-line editor and its file resolved NOW rather than at the next draw. */
RolltuiInput* rolltui_windows_input(RolltuiWindows* w, const char* source, size_t len);
RolltuiTranscript* rolltui_windows_transcript(RolltuiWindows* w, const char* source, size_t len);
RolltuiMenu* rolltui_windows_menu(RolltuiWindows* w, const char* source, size_t len);
/* Which rung answered for `menus/<source>.json`: the file's path, "the host's", "a shipped
 * menu", or "" when nothing did. Re-resolves first; a BORROW until the next refresh. */
const char* rolltui_windows_menu_origin(RolltuiWindows* w, const char* source, size_t len, size_t* out_len);

/* …and the same three by WINDOW id: the typed handle that window's widget draws, or NULL when
 * the window is unknown or its content is a different kind. `sync` fills the window table, so
 * a window that has never been synced answers NULL — which is what `content_at` already does
 * and is not an error. */
RolltuiMenu* rolltui_windows_menu_at(const RolltuiWindows* w, const char* window, size_t len);

/* THE SYNTAX HIGHLIGHTER every transcript this table owns renders code blocks through — a
 * HOST fact, off until set (`rolltui/Widgets.hpp` states the seam). Pushed straight onto every
 * transcript that already exists AND onto each one created afterwards, so the order a host
 * calls this and `rolltui_windows_transcript` in cannot matter and nothing has an epoch to
 * poll. `ctx` is released through `free_ctx` when this is called again or when `w` is freed —
 * the {fn, ctx, free_ctx} shape `rolltui_windows_bind_rows` and `rolltui_effect_register`
 * already use for a host's callable. */
void rolltui_windows_set_highlight(RolltuiWindows* w, RolltuiMdHighlightFn fn, void* ctx,
                                   void (*free_ctx)(void*));

/* THE EXTRA ROWS an `input:<source>` window must have whatever its text says (roll holds the
 * prompt as tall as the modal placed over it). Per-WINDOW sizing the widget keeps, not part of
 * the edited text the input owns — which is why it is set here by source name rather than on
 * the `RolltuiInput*` above. The widget is created on demand, so a host may set the floor
 * before any window has shown this input. */
void rolltui_windows_set_input_min_outer(RolltuiWindows* w, const char* source, size_t len, int rows);

/* That window's content string, a BORROW valid until the next `sync`. */
const char* rolltui_windows_content_at(const RolltuiWindows* w, const char* window, size_t len,
                                       size_t* out_len);

/* documents: `doc` is a BORROW this table never frees — the host, or `Windows`'s
 * `owned_documents_` for a sample built from markdown, keeps the `rolltui::Document` alive.
 * What crosses is `&doc->entries`, not `doc` itself: a `rolltui::Document` (Document.hpp) is
 * exactly one `RolltuiDocument` member and nothing else, so this table stores and hands back
 * the REAL type the transcript kind (`rolltui_widget_kinds.c`) reads directly — never an
 * opaque blob only C++ could interpret, the way this table's `const void*` used to work
 * before a pure-C kind needed to read one (Phase 17 m1c). */
void rolltui_windows_bind_document(RolltuiWindows* w, const char* name, size_t len, const RolltuiDocument* doc);

/* …and the OWNED half: one entry of verbatim markdown that this table keeps, for a tool
 * previewing ANOTHER app's sample content with no live document to point at (an app profile's
 * `documents`, `rolltui_app_profile_mount` below). Binding the same name twice replaces the
 * sample. STRATEGY 5 (GROWING HEAP): the `RolltuiDocument` is heap-held for the table's life,
 * because the borrow above needs a stable address and a sample outlives the call that set it.
 *
 * PHASE 17 m3: this used to be `Windows::owned_documents_`, the last C++ map left in that
 * class, kept there on the stated argument that moving it "would need either a fragile
 * reinterpret through `RolltuiDocument` or a second owner — neither pays for itself". That
 * argument was written while the C++ class was staying; the reinterpret is not needed at all
 * once the map is on this side, and the alternative to moving it is not a C++ map but NO
 * sample documents, since the class holding it is being deleted. It is the same sentence
 * shape as the two layout-hook comments that stopped being true the moment their subject
 * became the thing being removed. */
void rolltui_windows_bind_sample_document(RolltuiWindows* w, const char* name, size_t len, const char* markdown,
                                          size_t markdown_len);

/* Forward declarations: `RolltuiRows`' own inline C++ methods below call these before their
 * full declarations (right after the struct) would otherwise be seen. */
typedef struct RolltuiRows RolltuiRows;
void rolltui_rows_reset(RolltuiRows* r);
void rolltui_rows_add(RolltuiRows* r, const char* label, size_t label_len, const char* value, size_t value_len);
void rolltui_rows_release(RolltuiRows* r);

/* rows: one row of a `rows:` window — a label column and a value that wraps under it.
 * `rolltui::Row` IS this struct. */
typedef struct RolltuiRow {
  RolltuiStr label, value;
} RolltuiRow;

/* WHAT A HOST FILLS instead of returning a fresh vector every frame (Phase 13 m5b, and
 * CLAUDE.md's per-frame-API rule). `rolltui_rows_reset` keeps the array's capacity AND every
 * row's string buffers, so `rolltui_rows_add` on a warm frame assigns into storage that
 * already exists and allocates nothing. `rolltui::Rows` IS this struct. */
typedef struct RolltuiRows {
  RolltuiRow* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);   /* rows live; v[0..n) */
  size_t cap ROLLTUI_DEFAULT(0); /* rows allocated — v keeps its storage past n */
#ifdef __cplusplus
  void reset() { rolltui_rows_reset(this); }
  // rolltui's own shapes only (Phase 19 m2): a pointer and a length, or a RolltuiStr.
  void add(const char* label, std::size_t label_len, const char* value, std::size_t value_len) {
    rolltui_rows_add(this, label, label_len, value, value_len);
  }
  void add(const char* label, const char* value) { rolltui_rows_add(this, label, std::strlen(label), value, std::strlen(value)); }
  void add(const char* label, const char* value, std::size_t value_len) { rolltui_rows_add(this, label, std::strlen(label), value, value_len); }
  void add(const char* label, const RolltuiStr& value) { rolltui_rows_add(this, label, std::strlen(label), value.p, value.n); }
  void add(const RolltuiStr& label, const RolltuiStr& value) { rolltui_rows_add(this, label.p, label.n, value.p, value.n); }
  std::size_t size() const { return n; }
  const RolltuiRow& operator[](std::size_t i) const { return v[i]; }
  RolltuiRows() = default;
  RolltuiRows(const RolltuiRows&) = delete;
  RolltuiRows& operator=(const RolltuiRows&) = delete;
  ~RolltuiRows() { rolltui_rows_release(this); }
#endif
} RolltuiRows;

/* rows: `out` is the caller's `RolltuiRows`, filled in place — never stored past the call. */
typedef void (*RolltuiRowsFn)(void* ctx, RolltuiRows* out);
void rolltui_windows_bind_rows(RolltuiWindows* w, const char* name, size_t len, RolltuiRowsFn fn, void* ctx,
                               void (*free_ctx)(void*));

/* submit: `on_submit` is `Windows::OnSubmit` as an int (0 SendAndClear, 1 Keep) — this header
 * does not know the enum's name, only its two values, bound alongside the callable because a
 * host always sets both together. */
typedef void (*RolltuiSubmitFn)(void* ctx, const char* text, size_t len);
void rolltui_windows_bind_submit(RolltuiWindows* w, const char* name, size_t len, RolltuiSubmitFn fn, void* ctx,
                                 void (*free_ctx)(void*), int on_submit);

/* note: an input's one-line note, and what STATE it is in. A host with no motion to report
 * fills only `text`; `state` defaults to None, which is what makes the implicit conversion
 * from a bare string do the whole of "no motion" for a host that never mentions it.
 * `rolltui::Note` IS this struct. */
typedef struct RolltuiNote {
  RolltuiStr text;
#ifdef __cplusplus
  rolltui::EffectState state = static_cast<rolltui::EffectState>(0); /* None */
#else
  unsigned char state;
#endif
  unsigned long long since_ms ROLLTUI_DEFAULT(0); /* when it entered `state` */
#ifdef __cplusplus
  // No constructor, destructor or assignment of this type's OWN is declared beyond the
  // converting ones below: `text` (a `RolltuiStr`) already has correct copy/move/destroy, so
  // the compiler-generated special members already do the right thing by construction — the
  // same reasoning `RolltuiContent` states for itself.
  RolltuiNote() = default;
  // Implicit from a C string on purpose: a host with no motion to report writes
  // `return "working";`. Anything else sets the text by pointer and length — rolltui's own
  // shape, never a std:: one (Phase 19 m2).
  RolltuiNote(const char* t) : text(t) {}  // NOLINT(google-explicit-constructor)
  RolltuiNote(const char* t, std::size_t n, rolltui::EffectState s, unsigned long long since = 0) : state(s), since_ms(since) {
    text.assign(t, n);
  }
  // Text only: the state and its clock reset, which is what "a plain note" means.
  RolltuiNote& set(const char* t, std::size_t n) {
    text.assign(t, n);
    state = static_cast<rolltui::EffectState>(0);
    since_ms = 0;
    return *this;
  }
  RolltuiNote& operator=(const char* t) { return set(t, t ? std::strlen(t) : 0); }
#endif
} RolltuiNote;

void rolltui_note_clear(RolltuiNote* n); /* text = "", state = None, since_ms = 0; keeps the buffer */

/* note: `out` is the caller's `RolltuiNote`, already cleared, filled in place. */
typedef void (*RolltuiNoteFn)(void* ctx, RolltuiNote* out);
void rolltui_windows_bind_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNoteFn fn, void* ctx,
                               void (*free_ctx)(void*));

/* the preset directory: `file:`, `menu:`'s user rung and `menus/<name>.json` resolve against
 * this. A BORROW out, "" (never NULL) until `set_dir` is first called. */
void rolltui_windows_set_dir(RolltuiWindows* w, const char* dir, size_t len);
const char* rolltui_windows_dir(const RolltuiWindows* w, size_t* len);

/* a menu file the HOST carries in its own binary — `menu:<name>`'s middle rung (the order is
 * `Widgets.hpp`'s). The text is copied in; the borrow out is valid until that name is bound
 * again or `w` is freed. `_count`/`_name_at` enumerate every host menu for `Windows::menu_names`. */
void rolltui_windows_add_menu(RolltuiWindows* w, const char* name, size_t len, const char* json, size_t json_len);
size_t rolltui_windows_host_menu_count(const RolltuiWindows* w);
const char* rolltui_windows_host_menu_name_at(const RolltuiWindows* w, size_t i, size_t* len);

/* what a `help` window renders (Phase 15 m5, moved to the boundary so the `help` kind can be
 * a plugin like the rest): an optional lead line, the scopes to list in order, an optional
 * trailing note. `set_help` REPLACES lead/note; the scope list is built separately
 * (`clear_help_scopes` then `add_help_scope` per entry) because it is a `std::vector` at the
 * one C++ call site (`Windows::set_help`) and this is the same shape `rolltui_windows_add_menu`
 * already uses for a list built one call at a time. */
void rolltui_windows_set_help(RolltuiWindows* w, const char* lead, size_t lead_len, const char* note,
                              size_t note_len);
void rolltui_windows_clear_help_scopes(RolltuiWindows* w);
void rolltui_windows_add_help_scope(RolltuiWindows* w, const char* scope, size_t len);

typedef struct RolltuiWidgetEnv {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  unsigned long long now_ms ROLLTUI_DEFAULT(0);
} RolltuiWidgetEnv;

void rolltui_windows_set_env(RolltuiWindows* w, const RolltuiWidgetEnv* env);

/* THE LIVE BINDINGS TABLE, as a handle (Phase 15 m5e): a widget looks up an action's chords
 * or asks whether a key is one of the transcript scope's without knowing `rolltui::Bindings`
 * exists. A BORROW — `w` never frees it — set once per frame alongside `set_env` from
 * `Bindings::handle()` (the live table, or `default_bindings().handle()`). NULL only before
 * the first `set_env`. */
void rolltui_windows_set_bindings(RolltuiWindows* w, const RolltuiBindings* b);

/* THE CURRENT FRAME'S STYLE TABLE, indexed by Role ordinal — a BORROW valid for the length of
 * one `rolltui_windows_draw` call, set at its top from the `styles` it is already handed (the
 * same array `draw_scrollbar` inside this module reads). This is what lets a widget's `draw`
 * ask "what does Role::text look like" without the vtable's `draw` slot carrying a fourth
 * parameter every kind must accept whether or not it draws text — the `RolltuiWindowRoles`
 * shape one level up, generalised to the one thing every drawing kind needs. NULL outside a
 * draw call. */
const RolltuiStyle* rolltui_windows_styles(const RolltuiWindows* w);

/* Instantiates/reuses a widget per window and collects what each one says is wrong. The
 * report's lines are BORROWS, valid until the next sync. */
void rolltui_windows_sync(RolltuiWindows* w, const RolltuiWindowStack* stack);
size_t rolltui_windows_report_count(const RolltuiWindows* w);
const char* rolltui_windows_report_at(const RolltuiWindows* w, size_t i, size_t* len);

/* The one-line form: the first bad value, plus " (+N more)" when there are others; "" when
 * there are none. `rolltui::WindowsReport::summary()`'s rule, moved here in Phase 17 m2a
 * because the struct that held it is deleted in m2c and every host draws this string.
 * APPENDS to `out`. */
void rolltui_windows_report_summary(const RolltuiWindows* w, RolltuiStr* out);

/* Asks each widget for the outer extent it wants and writes it into the node (the only thing
 * a widget writes back into the layout tree). */
void rolltui_windows_autosize(RolltuiWindows* w, RolltuiWindowStack* stack, RolltuiRect box);
void rolltui_windows_layout(RolltuiWindows* w, const RolltuiWindowStack* stack, RolltuiRect box);

/* THE TWO ROLES THE WINDOW ITSELF DRAWS WITH. The widget draws with its own; these are the
 * scrollbar's track and thumb, which live in the window's border column and which a widget
 * never sees (Phase 12 m5). */
typedef struct RolltuiWindowRoles {
  unsigned char scrollbar;
  unsigned char border;
  unsigned char border_active;
} RolltuiWindowRoles;

/* The library's own three (Phase 17 m3), for the same reason `rolltui_layout_default_roles`
 * exists: these three bytes were `Widgets.cpp`'s `kWindowRoles`, that file is deleted, and
 * every host calls `rolltui_windows_draw`. BORROWS static storage; a host that paints its
 * scrollbar from another role still passes its own struct. */
const RolltuiWindowRoles* rolltui_windows_default_roles(void);

void rolltui_windows_draw(RolltuiWindows* w, const RolltuiResolvedNode* rn, RolltuiFrame* f,
                          const RolltuiStyle* styles, const RolltuiWindowRoles* roles);
/* An event the stack routed to `window`; 1 when the widget (or its scrollbar) consumed it. */
int rolltui_windows_handle(RolltuiWindows* w, const char* window, size_t len, const RolltuiEvent* e);


/* ========================================================================================
 * app_profile — what a layout may name in an app: written by one host, read by another
 * ======================================================================================== */
/* The three-way source rule a registered kind states (`AppProfile.hpp`'s `Kind::rule`,
 * `rolltui::SourceRule` in `Layout.hpp`). Named separately from `rolltui_layout.h`'s
 * `ROLLTUI_SOURCE_*` rather than reusing them, the same way `AppProfile.cpp`'s original
 * `rule_name`/`rule_from_name` table was already self-contained rather than reaching into
 * Layout for one: nothing here needs the rest of `rolltui_layout.h`'s (much larger) surface,
 * and the numeric VALUES matching is a documented fact the C++ shim converts through an
 * explicit switch, never a `static_cast` relying on the two staying numbered alike. */
#define ROLLTUI_APP_PROFILE_SOURCE_REQUIRED 0
#define ROLLTUI_APP_PROFILE_SOURCE_OPTIONAL 1
#define ROLLTUI_APP_PROFILE_SOURCE_FORBIDDEN 2

typedef struct RolltuiAppProfile RolltuiAppProfile; /* opaque; see the header comment */

/* ---- the report: unknown keys and bad values are warnings, not failures ---------------
 * Transparent (unlike `RolltuiAppProfile`) because nothing about it needs hiding — it is
 * exactly `RolltuiStr` values in GROWING AMORTISED arrays, the same shape
 * `rolltui_presets.c`'s own `NameList` uses for "an array of small owned strings". Zero-
 * initialise before use (`RolltuiAppProfileReport r = {0};`), the same rule `RolltuiStr`
 * itself states. */
typedef struct RolltuiAppProfileReport {
  RolltuiStr error; /* non-empty: unusable, and `rolltui_app_profile_parse` returns NULL */
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
} RolltuiAppProfileReport;

void rolltui_app_profile_report_release(RolltuiAppProfileReport* r); /* frees everything; zeroes it */
int rolltui_app_profile_report_clean(const RolltuiAppProfileReport* r);
/* Mirrors `AppProfileReport::summary()` exactly: empty when clean; otherwise the error, or
 * "bad: x; unknown: y" joined the same way. Replaces `*out`. */
void rolltui_app_profile_report_summary(const RolltuiAppProfileReport* r, RolltuiStr* out);

/* Parses a profile from its file's TEXT. Returns an OWNED profile the caller frees with
 * `rolltui_app_profile_free`, or NULL when it is unusable (a JSON syntax error, the JSON is
 * not an object, or it has no "app" name) — `report->error` says which. A profile with
 * unknown keys or malformed entries is still returned; those go into `report` as warnings,
 * the same non-fatal/fatal split `load_app_profile` already made. `report` is reset by this
 * call (as if freshly zero-initialised) whether it succeeds or fails. */
RolltuiAppProfile* rolltui_app_profile_parse(const char* text, size_t len, RolltuiAppProfileReport* report);
void rolltui_app_profile_free(RolltuiAppProfile* p); /* a no-op on NULL */

/* Serialises to TEXT, REPLACING `*out` — never a `RolltuiJsonValue*` handed back for the
 * caller to dump itself, which is the fix this module makes (see the header comment).
 * indent = 0 -> single line, matching `rolltui_json_dump`. */
void rolltui_app_profile_dump(const RolltuiAppProfile* p, int indent, RolltuiStr* out);

/* ---- building one from scratch: what `AppProfile.cpp`'s shim calls when a C++ host has
 * already built a `rolltui::AppProfile` by hand (`TuiFrontend.cpp`'s `roll_app_profile()`,
 * `paint.cpp`'s `paint_profile()`) and needs a `RolltuiAppProfile*` to hand to `_dump`.
 * `rolltui_app_profile_parse` above is built out of these same primitives while walking
 * parsed JSON, so there is exactly one way a profile's fields get set, read by two callers
 * rather than duplicated for each. ---------------------------------------------------------- */
RolltuiAppProfile* rolltui_app_profile_new(void);
void rolltui_app_profile_set_app(RolltuiAppProfile* p, const char* s, size_t len);
void rolltui_app_profile_set_min_size(RolltuiAppProfile* p, int width, int height);
void rolltui_app_profile_add_action(RolltuiAppProfile* p, const char* name, size_t name_len, const char* desc,
                                    size_t desc_len);
void rolltui_app_profile_add_kind(RolltuiAppProfile* p, const char* name, size_t name_len, int rule,
                                  const char* describes, size_t describes_len);
void rolltui_app_profile_add_document(RolltuiAppProfile* p, const char* name, size_t name_len, const char* sample,
                                      size_t sample_len);
/* Returns the new row's index, so its samples can be added with `_row_add_sample`. */
size_t rolltui_app_profile_add_row(RolltuiAppProfile* p, const char* name, size_t name_len);
void rolltui_app_profile_row_add_sample(RolltuiAppProfile* p, size_t row_i, const char* label, size_t label_len,
                                       const char* value, size_t value_len);
void rolltui_app_profile_add_submit(RolltuiAppProfile* p, const char* s, size_t len);
void rolltui_app_profile_add_note(RolltuiAppProfile* p, const char* s, size_t len);
void rolltui_app_profile_add_menu(RolltuiAppProfile* p, const char* name, size_t name_len, const char* json,
                                  size_t json_len);
void rolltui_app_profile_set_help(RolltuiAppProfile* p, const char* lead, size_t lead_len, const char* note,
                                  size_t note_len);
void rolltui_app_profile_add_help_scope(RolltuiAppProfile* p, const char* s, size_t len);

const char* rolltui_app_profile_app(const RolltuiAppProfile* p, size_t* len);
int rolltui_app_profile_min_width(const RolltuiAppProfile* p);
int rolltui_app_profile_min_height(const RolltuiAppProfile* p);

size_t rolltui_app_profile_action_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_action_name(const RolltuiAppProfile* p, size_t i, size_t* len);
const char* rolltui_app_profile_action_description(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_kind_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_kind_name(const RolltuiAppProfile* p, size_t i, size_t* len);
int rolltui_app_profile_kind_rule(const RolltuiAppProfile* p, size_t i); /* ROLLTUI_APP_PROFILE_SOURCE_* */

size_t rolltui_app_profile_document_count(const RolltuiAppProfile* p);

size_t rolltui_app_profile_submit_count(const RolltuiAppProfile* p);

size_t rolltui_app_profile_menu_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_menu_name(const RolltuiAppProfile* p, size_t i, size_t* len);
const char* rolltui_app_profile_menu_json(const RolltuiAppProfile* p, size_t i, size_t* len);

size_t rolltui_app_profile_help_scope_count(const RolltuiAppProfile* p);
const char* rolltui_app_profile_help_scope_at(const RolltuiAppProfile* p, size_t i, size_t* len);

/* ---- what a layout may NAME in this app, and MOUNTING it (Phase 17 m3) ---------------------
 * Both were `AppProfile.cpp`'s, left behind by m1 because they touch `Windows` rather than the
 * profile's own serialising algorithm. They are here because they are the profile's MEANING —
 * the answer to "what may a screen for this app say?" — and the alternative to moving them was
 * not a C++ home but no home at all.
 *
 * `_content_count`/`_content_at` enumerate the contents an authoring tool may OFFER: one per
 * document ("transcript:NAME"), submit ("input:NAME"), row source ("rows:NAME") and menu
 * ("menu:NAME"), then bare "help", one "help:SCOPE" per declared scope, and one per registered
 * kind. `*out` is REPLACED and the caller owns it.
 *
 * DELIBERATELY NOT `rolltui_content_format`: a kind that takes a source is offered as
 * "NAME:" — a template with the source left for the author to type — for BOTH Required and
 * Optional, where the join rule would drop the colon on an Optional kind with no source. These
 * are two different questions ("what does this resolved content spell as?" versus "what should
 * a picker put in the field?") and collapsing them would silently offer an Optional kind with
 * no way to see it takes a source. */
size_t rolltui_app_profile_content_count(const RolltuiAppProfile* p);
void rolltui_app_profile_content_at(const RolltuiAppProfile* p, size_t i, RolltuiStr* out);

/* Mounts the profile into a window table so a tool renders as the TARGET app: its kinds as
 * PLACEHOLDER widgets (never an error panel — the window is correct, this tool simply is not
 * the app that can build it), its documents as sample content the table owns, its row sources
 * as their recorded samples, its submits and notes as no-ops, its menu files verbatim, and its
 * help scopes in place of the tool's own when it declared any.
 *
 * ONE CALL, so a tool's wiring cannot half-apply a profile — which is the whole property the
 * function exists for and the reason it is not six calls a host makes in an order it chooses.
 * `w` must outlive nothing in `p`: everything crossing here is COPIED. */
void rolltui_app_profile_mount(const RolltuiAppProfile* p, RolltuiWindows* w);


/* ========================================================================================
 * diff — diff highlighting a host draws
 * ======================================================================================== */
/* One span of ONE line, as byte offsets into that line. `role` is a `rolltui::Role` value,
 * taken from the RolltuiDiffRoles the caller handed in and never invented here. */
typedef struct RolltuiDiffSpan {
  size_t begin, end; /* end exclusive */
  unsigned char role;
} RolltuiDiffSpan;

/* A line takes its whole role, or splits into line / word / line around its changed run —
 * so three is the maximum for any line, in any configuration, forever. A caller sizes its
 * output buffer from this constant and never asks first. */
#define ROLLTUI_DIFF_MAX_SPANS 3

/* The seven roles this module may emit, handed in by the caller (see the note above).
 * `file_header` is the `---`/`+++` pair, which is NOT an added or removed line; `hunk` is
 * `@@`, a position rather than a change. */
typedef struct RolltuiDiffRoles {
  unsigned char added, removed, context, file_header, hunk, added_word, removed_word;
} RolltuiDiffRoles;

/* THE MAPPING ITSELF, as a BORROW of a static table — the tenth vocabulary this phase has
 * brought home, and it arrived the way the other nine did: by converting a consumer.
 *
 * It lived in `Diff.cpp`'s anonymous namespace under this reason: *"`Role` is the styling
 * vocabulary of a layer that has not been ported, and mirroring the enum in a C header would be
 * a second definition of it."* **That was true when written and is not now.** `Role` IS ported
 * — `ROLLTUI_ROLE_LIST` in `rolltui_style.h`, whose own note reads "ONE SPELLING, and it is this
 * list. Both languages DERIVE from it" — so naming a role here mirrors nothing.
 *
 * And the tell had already fired: `rolltui/tests/markdown_test.cpp` carried a verbatim second
 * copy, and `lifetime_test`'s conversion was about to make a third before it stopped and
 * reported instead. Two consumers writing the same table means the API is wrong, not the
 * consumers. A caller that wants a DIFFERENT mapping still passes its own — this is the
 * default, not a replacement for the parameter. */
const RolltuiDiffRoles* rolltui_diff_default_roles(void);

/* The block's lines, read on demand. Returns a BORROW of line `i`, valid for the duration
 * of the call; `*len` receives its length. A zero-length line gives a valid pointer. */
typedef const char* (*RolltuiDiffLineFn)(const void* block, size_t i, size_t* len);

/* ---- working memory ------------------------------------------------------------------ */
/* The decode, boundary and token-range buffers the word-level refinement needs, owned by
 * the caller and reused across calls: one handle per thread, made once, grown to a
 * high-water mark over the first few calls and never again. One buffer per ROLE, so the
 * two sides of a pair cannot alias each other's token ranges.
 *
 * It owns its own `RolltuiUnicodeScratch` (created lazily, as the wrap engine's handle
 * does), which is what keeps this boundary at ONE handle for a caller to hold. */
typedef struct RolltuiDiffScratch RolltuiDiffScratch;
RolltuiDiffScratch* rolltui_diff_scratch_new(void);
void rolltui_diff_scratch_free(RolltuiDiffScratch* s);

/* True for the info strings this colouriser answers to: "diff", "patch", "udiff". The
 * FENCE decides; content is never sniffed (Diff.hpp). */
int rolltui_diff_is_language(const char* lang, size_t lang_len);

/* The spans of `block[index]`, into `out` (capacity `out_cap`, at least
 * ROLLTUI_DIFF_MAX_SPANS). Returns how many were written: 0 for a language this does not
 * claim and for an index past the block. */
size_t rolltui_diff_spans(RolltuiDiffScratch* s, const char* lang, size_t lang_len, const void* block,
                          size_t line_count, RolltuiDiffLineFn line_at, size_t index,
                          const RolltuiDiffRoles* roles, RolltuiDiffSpan* out, size_t out_cap);


/* ========================================================================================
 * embedded — the shipped files, by name
 * ======================================================================================== */
/* BOTH FIELDS ARE BARE `const char*`, AND A C++ CALLER MUST NOT COMPARE THEM WITH `==`.
 * `table[i].name == "default"` compiles and compares POINTERS. It cost an afternoon on
 * 2026-09-04: Release passed 51/51 because the compiler merged the identical literals so the
 * pointers really were equal, and the RelWithDebInfo sanitizer build — which did not merge
 * them — failed 14 of 31 suites. The green was not stale; it was true and useless.
 *
 * Everywhere else in this library an owned string is `RolltuiStr`, which carries
 * `operator==(const char*)` and does the right thing. This struct cannot be one: it is a
 * static table of literals that owns nothing. So the obligation moves to the caller —
 * **wrap in `std::string_view` before comparing**, and prefer `rolltui_embedded_text()`
 * below, which does the comparison correctly once so no caller has to. */
typedef struct RolltuiEmbeddedFile {
  const char* name; /* the file's stem: "default", "no-panel" */
  const char* text; /* its bytes, NUL-terminated */
} RolltuiEmbeddedFile;

/* The library's four shipped sets, and their counts. A HOST embedding its own files passes
 * its own PREFIX to the generator, so its table cannot collide with these. */
extern const RolltuiEmbeddedFile rolltui_kThemePresets[];
extern const size_t rolltui_kThemePresetCount;
extern const RolltuiEmbeddedFile rolltui_kLayoutPresets[];
extern const size_t rolltui_kLayoutPresetCount;
extern const RolltuiEmbeddedFile rolltui_kMenus[];
extern const size_t rolltui_kMenuCount;
extern const RolltuiEmbeddedFile rolltui_kBindingsPresets[];
extern const size_t rolltui_kBindingsPresetCount;

/* The text of one shipped file by name, or NULL. A BORROW, valid for the process. */
const char* rolltui_embedded_text(const RolltuiEmbeddedFile* table, size_t count, const char* name,
                                  size_t name_len);


/* ========================================================================================
 * lifetime — the release point
 * ======================================================================================== */
/* Registers a releaser to run at `rolltui_shutdown()`, in reverse order of registration.
 * Registering the same function twice registers it twice; register once, where the thing
 * is made. */
void rolltui_on_shutdown(void (*fn)(void));

/* Releases everything the library retains: every registered process-wide releaser (most
 * recently registered first), then every thread's own scratch via `rolltui_release_thread`
 * (which here can only release the CALLING thread's — the same limit `release_thread()`
 * has always had, since another thread's `_Thread_local` storage cannot be freed from
 * here). Safe to never call and safe to call twice — the second call finds nothing to do.
 * Nothing is invalidated for a host that carries on afterwards; the caches simply rebuild
 * on next use, which is what makes this safe to call at any time rather than only at the
 * very end. */
void rolltui_shutdown(void);

/* Releases just the calling thread's scratch buffers — the reused storage the draw path
 * lends (`rolltui/Scratch.hpp`). This is the explicit path for a long-lived thread that
 * wants to hand back its high-water mark, and for a leak check that needs the number to
 * be knowable BEFORE the process ends. A thread that is about to exit need not call it. */
void rolltui_release_thread(void);



/* ========================================================================================
 * marker — the "N more" rule
 * ======================================================================================== */
/* It SHORTENS rather than eating the line (Phase 12 m5). The marker writes over CONTENT
 * cells, and at 20 cells wide the full form took most of the row ("│   Ent▼ 187 more │").
 * Shortening is only safe because the scrollbar carries the proportion: the two are KEPT
 * TOGETHER on purpose — the bar is the positional signal and the marker is the
 * NON-GRAPHICAL one, which is the first thing a mono theme, a low colour depth or a
 * borderless window still has. Writes 0 bytes when there is nothing below or no room.
 *
 * `out` needs ROLLTUI_MARKER_MAX; the count is a `size_t`, so twenty digits is the bound. */
#define ROLLTUI_MARKER_MAX 40
size_t rolltui_scroll_marker_text(size_t below, int max_width, int ambiguous_wide, char* out, size_t cap);


/* ========================================================================================
 * mem — the leak gauge — public by stated reason: it is how a consumer proves it released everything
 * ======================================================================================== */
/* Resets the CUMULATIVE counters only (`rolltui_mem_alloc`'s `allocations`, `frees` and
 * `bytes_requested`, as `rolltui_mem_stats` reports them). `live_bytes` and `live_blocks`
 * describe storage that still exists and zeroing them would be a lie; `peak_bytes` is
 * re-based to whatever is currently live, which is the lowest value it could honestly
 * take. For a test that wants a window; never called by the library itself. */
/* MEMORY USAGE, QUERYABLE AT RUNTIME — the allocator's own counters, in
 * the shape this boundary uses everywhere: out-params, any of which may be NULL, so a caller
 * asks for exactly the numbers it means to show. Phase 13's requirement was that these be
 * readable at RUNTIME and not only inside a test binary — one pipeline, two consumers, the
 * human-facing pane and the router's own adaptation reading the same records.
 *
 * The three byte numbers answer three different questions and are deliberately not collapsed:
 *   bytes_requested  CUMULATIVE and never decreasing — a growing buffer is counted again at
 *                    every growth. A churn signal, NOT how much is held.
 *   live_bytes       HELD RIGHT NOW, as the allocator's usable size. This is the one a status
 *                    pane means by "memory usage".
 *   peak_bytes       the high-water mark of live_bytes — for a library built on reusing
 *                    buffers, how big the reuse ever had to get. */
void rolltui_mem_stats(size_t* allocations, size_t* frees, size_t* bytes_requested,
                       size_t* live_bytes, size_t* peak_bytes, size_t* live_blocks);

/* ---- the library's one entry point, and the only two halves of it a CONSUMER may name -----
 * Moved here from `rolltui/c/rolltui_alloc.h` on 2026-09-05 (Phase 17 m3): that header is
 * internal and the umbrella excludes it, so a consumer that wanted a handle and its release
 * could not reach one — while `rolltui_alloc.h`'s own text said they "stay available
 * everywhere". `rolltui_mem_realloc` deliberately did NOT come with them: growth is the thing
 * the closed set exists to stop being invented, and leaving its declaration in an internal
 * header makes that structural rather than a grep control's promise.
 *
 * An allocation failure ABORTS rather than returning NULL, so neither can fail and no caller
 * checks. Every allocation in the library goes through these two — CLAUDE.md's rule, and the
 * reason `rolltui_mem_stats` above can be believed. */
void* rolltui_mem_alloc(size_t bytes);
void rolltui_mem_free(void* p);


/* ========================================================================================
 * presets — the preset stores and domains
 * ======================================================================================== */
/* Reads a whole file. 0 when it cannot be opened. `put`/`ctx` rather than a `RolltuiStr*`
 * because the library's own three callers stream into their internal `Buf`; a consumer that
 * wants the bytes passes `rolltui_str_put` and a `RolltuiStr*`. */
int rolltui_preset_read_file(const char* path, size_t path_len, RolltuiPutFn put, void* ctx);
/* Writes to a sibling temp file, then renames — a reader sees the old complete file or the
 * new complete file, never a mix (the state-file rule from ResilientModelManager). On
 * failure, 0, and the reason REPLACES `*err` (which may be NULL). */
int rolltui_preset_write_file_atomic(const char* path, size_t path_len, const char* bytes, size_t len,
                                     RolltuiStr* err);

/* The report, as five things the mechanics do to one. `report` is whatever the caller passed
 * in; this file never dereferences it. */
typedef struct RolltuiPresetReportFns {
  void (*reset)(void* report);
  void (*set_error)(void* report, const char* s, size_t len);
  /* The current error, so the store can wrap a path around it and set it back. */
  void (*get_error)(const void* report, RolltuiPutFn put, void* ctx);
  void (*add_note)(void* report, const char* s, size_t len);
  /* Every note gets `prefix` in front of it — `parse_partial` says what it kept, and the
   * store says which file it was. */
  void (*prefix_notes)(void* report, const char* prefix, size_t len);
  /* A report of THIS domain's type, made and unmade by the domain (Phase 18 m3) — OWNED,
   * short-lived, through the library's entry point. What lets the mechanics parse into a
   * report of their own instead of asking every caller for a second one it throws away. */
  void* (*create)(void);
  void (*destroy)(void* report);
} RolltuiPresetReportFns;

/* One domain: its four names, its embedded shipped table, and what can be done to a value.
 * `cache` is OWNED by this descriptor and built on first use — see the note on
 * `rolltui_preset_domain_release`. */
typedef struct RolltuiPresetShippedCache RolltuiPresetShippedCache;

typedef struct RolltuiPresetDomain {
  const char* kind;         /* "theme" | "layout" | "bindings" — for messages */
  size_t kind_len;
  const char* working_file; /* "theme.working.json" */
  size_t working_file_len;
  const char* subdir;       /* "themes" */
  size_t subdir_len;

  size_t (*shipped_count)(void);
  void (*shipped_at)(size_t i, const char** name, size_t* name_len, const char** text, size_t* text_len);

  /* TEXT in, an OWNED value out (NULL on failure, with the report saying why). The JSON
   * never crosses this boundary — see the note at the top. */
  void* (*parse)(const char* text, size_t len, void* report);
  /* A PARTIAL file (a colours-only theme file): fills part of `working`, notes why; NULL
   * when the file is not partial, and `parse` is then used. */
  void* (*parse_partial)(const char* text, size_t len, const void* working, void* report);
  void (*to_json)(const void* value, const char* name, size_t name_len, RolltuiPutFn put, void* ctx);
  /* …and the same, with the working copy's ORIGIN written into it as "preset". One call
   * rather than a second serialiser, because that key is the store's and not the domain's. */
  void (*to_json_with_origin)(const void* value, const char* name, size_t name_len, RolltuiPutFn put, void* ctx);
  /* …and back: the preset name a working-copy file says it came from, "default" when it
   * says nothing. The other half of the same key, and the reason it is a callback rather
   * than something the store reads itself is the same one — the JSON never crosses. */
  void (*origin_of)(const char* text, size_t len, RolltuiPutFn put, void* ctx);

  void* (*clone)(const void* value);
  void (*destroy)(void* value);
  int (*equal)(const void* a, const void* b);

  /* The ops for this domain's REPORT type — BORROWED, a library static, set by the domain's
   * `_init`. Until Phase 18 m3 this travelled as a separate parameter beside the domain at
   * every `_new` and `_shipped` call, and no caller ever paired a domain with any table but
   * its own: two things that always travel together are one thing. */
  const RolltuiPresetReportFns* report;

  RolltuiPresetShippedCache* cache;
} RolltuiPresetDomain;

/* The shipped presets, parsed once per domain, into a report the domain makes for itself. A
 * shipped preset that does not load cleanly is a programming error (the layout loader's
 * standard): it says so and aborts, and so does a domain with no preset named "default"
 * (rule 5). Returns a BORROW, valid until the domain is released. NULL for a name that is not
 * shipped. */
const void* rolltui_preset_shipped(RolltuiPresetDomain* d, const char* name, size_t len);
int rolltui_preset_is_shipped(RolltuiPresetDomain* d, const char* name, size_t len);
/* The shipped preset's FILE TEXT, verbatim — a BORROW of the embedded bytes, valid for the
 * process's life; NULL (and `*out_len` 0) when `name` is not shipped.
 *
 * ADDED Phase 17 m4b, because its absence was being paid for: `shipped_count`/`shipped_at`
 * gave every index but no way to ask by NAME, so roll and the studio each hand-wrote the same
 * linear search over them. A lookup the API can do and does not offer is a lookup every
 * consumer writes. */
const char* rolltui_preset_shipped_text(RolltuiPresetDomain* d, const char* name, size_t len, size_t* out_len);
/* The shipped names, "default" FIRST and the rest in table order — the order a chooser
 * offers them in. REPLACES `*out`. */
void rolltui_preset_shipped_names(RolltuiPresetDomain* d, RolltuiStrList* out);
/* Releases the parsed cache. A domain descriptor is a process-wide static on the other
 * side, so this is what it hands back at `rolltui::shutdown()`; the cache rebuilds on next
 * use, which is what makes shutdown callable at any moment. */
void rolltui_preset_domain_release(RolltuiPresetDomain* d);

/* ---- the store ------------------------------------------------------------------------------ */
/* OWNED, LONG-LIVED: one per `rolltui::PresetStore<D>`, which frees it. Every method takes
 * the store's own lock and hands back copies, so a host may edit from one thread and render
 * from another. */
typedef struct RolltuiPresetStore RolltuiPresetStore;

RolltuiPresetStore* rolltui_preset_store_new(RolltuiPresetDomain* d, const char* dir, size_t dir_len,
                                             int may_write_shipped, const char* shipped_dir, size_t shipped_dir_len);
void rolltui_preset_store_free(RolltuiPresetStore* s);

/* Startup: the autosaved working copy when present and loadable, else "default". `report`
 * says what happened to the working file; the origin preset it names is re-read into a
 * report the domain makes for itself (`RolltuiPresetReportFns::create`), which until Phase
 * 18 m3 was a second report every caller had to supply and could — with no guard — pass the
 * same report for, which reset the first one mid-way. */
void rolltui_preset_store_start(RolltuiPresetStore* s, void* report);

/* A CLONE the caller owns and frees with `rolltui_preset_store_value_free`. */
void* rolltui_preset_store_working(const RolltuiPresetStore* s);
/* Frees a value `_working` or `_get` handed back: the store's own domain's `destroy`, so a
 * caller holding the store alone can release what the store gave it. Sixteen call sites in
 * four consumers had reached past the store to the domain descriptor for this (Phase 18 m3)
 * — `rolltui.h` rule 1 says a handle is created and released IN A PAIR, and `_working` had
 * no partner. NULL is a no-op. */
void rolltui_preset_store_value_free(const RolltuiPresetStore* s, void* v);
/* BORROWS of the store's own bytes, valid until it next changes. */
const char* rolltui_preset_store_origin(const RolltuiPresetStore* s, size_t* len);
const char* rolltui_preset_store_last_error(const RolltuiPresetStore* s, size_t* len);
/* "(modified)" is BY COMPARISON (rule 4) — and the comparison runs when the store CHANGES,
 * never when this is read: a read is one flag under the lock. Until 2026-09-06 it ran the
 * domain's deep `equal` over the whole working copy on every call, and roll's status panel
 * reached it twice per frame through `label` below — found by the first instrument pointed at
 * roll's own frame (`tests/status_budget_test.cpp`), not by reading. */
int rolltui_preset_store_modified(const RolltuiPresetStore* s);

/* "<origin>", or "<origin> (modified)" once the working copy differs from what it was loaded
 * from — `rolltui::PresetStore::label()`'s composition, and the SECOND English sentence this
 * module owned that lived one level up (Phase 17 m2a, found while measuring m2c: the first
 * sweep for unported English read every `.cpp` and no `.hpp`, and `PresetStore.hpp` is a
 * template header where the whole store lives inline). `studio.cpp:1173` already spells
 * " (modified)" a second time, for a LAYOUT's own name rather than for a store's origin — one
 * word, two spellings, exactly the shape this phase keeps finding. REPLACES `*out`, reusing its
 * buffer — the shape every other "text out" in this header has (`_working_path`,
 * `_preset_path`); it APPENDED until 2026-09-06, which made it the one a caller holding a
 * buffer for its frame could not call twice. */
void rolltui_preset_store_label(const RolltuiPresetStore* s, RolltuiStr* out);
unsigned long long rolltui_preset_store_version(const RolltuiPresetStore* s);

/* The only write anyone does (rule 2): replace the working copy. TAKES OWNERSHIP of `v`. */
void rolltui_preset_store_set_working(RolltuiPresetStore* s, void* v, int persist);
/* An in-place edit under the lock. */
void rolltui_preset_store_edit(RolltuiPresetStore* s, void (*fn)(void* value, void* ctx), void* ctx, int persist);

/* ---- ONE PRESET IN A LISTING, and the shape every "N things out" uses ---------------------
 *
 * THIS TYPE EXISTS BECAUSE ITS ABSENCE WAS BEING PAID FOR THREE TIMES (Phase 17 m4b,
 * 2026-09-05). `rolltui_preset_store_list` took a SINK — `(void* ctx, const char* name,
 * size_t, int shipped, const char* path, size_t)` — and that parameter list IS a struct
 * definition the library declined to write down. So every consumer wrote it instead:
 * `roll::PresetInfo` in `include/TuiFrontend.hpp`, `PresetInfo` in `rolltui/tools/studio.cpp`
 * and `PresetInfo` in `rolltui/tests/presets_test.cpp` — three byte-identical structs, each
 * with a lambda, a `static_cast<std::vector<PresetInfo>*>` and a collector around it.
 *
 * That is rule 5 of `rolltui.h` firing ("if two consumers write the same wrapper, the API is
 * wrong, not the consumers"), and the fix is not a C++ layer over the sink — it is naming the
 * thing the sink was spelling out.
 *
 * THE RULE THIS SETTLES, and it is the one `rolltui.h` had for TEXT OUT and not for N THINGS
 * OUT: a result the library ALREADY HAS goes into a buffer the CALLER owns and reuses —
 * `RolltuiStr*` for text, a growing list like this for many things — and is REPLACED on every
 * call. A callback is for a DECISION the library cannot make (`RolltuiScopeFn`,
 * `RolltuiRowsFn`, `RolltuiEffectFn`), never for handing back an answer. The two are told
 * apart by one question: does the callback carry a decision IN, or a result OUT?
 *
 * The shipped ones come first, "default" ahead of the rest — the order a chooser offers them
 * in — then the user's "*.json" that do not shadow a shipped name. `path` is empty for a
 * shipped preset. */
typedef struct RolltuiPresetInfo {
  RolltuiStr name;
  RolltuiStr path; /* "" for a shipped preset */
  int shipped ROLLTUI_DEFAULT(0);
} RolltuiPresetInfo;

/* A caller-owned, reusable list of them. Zero-initialise before first use; `_release` frees
 * everything and zeroes it (a no-op on a zeroed list, and on NULL). In C++ the destructor
 * does that, so a plain local needs no release call at all. */
typedef struct RolltuiPresetList {
  RolltuiPresetInfo* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiPresetList() = default;
  RolltuiPresetList(const RolltuiPresetList&) = delete;
  RolltuiPresetList& operator=(const RolltuiPresetList&) = delete;
  ~RolltuiPresetList();
  const RolltuiPresetInfo* begin() const { return v; }
  const RolltuiPresetInfo* end() const { return v + n; }
  size_t size() const { return n; }
  bool empty() const { return n == 0; }
  const RolltuiPresetInfo& operator[](size_t i) const { return v[i]; }
#endif
} RolltuiPresetList;

void rolltui_preset_list_release(RolltuiPresetList* l);

/* REPLACES `*out` (its capacity, and each entry's string buffers, are reused). */
void rolltui_preset_store_list(const RolltuiPresetStore* s, RolltuiPresetList* out);

/* A preset by name or path, as a value the caller OWNS and frees with
 * `rolltui_preset_store_value_free`; NULL with the report saying why. */
void* rolltui_preset_store_get(const RolltuiPresetStore* s, const char* name, size_t len, void* report);
/* …and the same, into the working copy. 0 when it could not be read. */
int rolltui_preset_store_load(RolltuiPresetStore* s, const char* name, size_t len, void* report, int persist);

#define ROLLTUI_SAVE_SAVED 0
#define ROLLTUI_SAVE_REFUSED_SHIPPED 1
#define ROLLTUI_SAVE_EXISTS_ASK 2
#define ROLLTUI_SAVE_BAD_NAME 3
#define ROLLTUI_SAVE_WRITE_FAILED 4
/* The SENTENCE for an outcome (Phase 17 m2a). The comment below used to end "...is a fixed
 * sentence per outcome and is built one level up, where the words already are" — the same
 * sentence, in the same shape, as the four other places this phase has had to reverse: the
 * words were one level up in `Presets.cpp`, which m2c deletes. A fixed sentence per outcome is
 * a table, and a table belongs with the constants it is indexed by. BORROWS a static literal;
 * `*len` may be NULL; an out-of-range code reads back as "". WRITE_FAILED's own reason is
 * still the caller's, through `err` below — that one is not fixed. */
const char* rolltui_preset_save_result_text(int result, size_t* len);

/* Save-as. REPLACES `*err` (which may be NULL when the caller does not want it) with the
 * outcome's SENTENCE for every result but SAVED — WRITE_FAILED's own reason, and
 * `rolltui_preset_save_result_text`'s fixed sentence for the other three — so a caller reads
 * one string for any refusal. Until Phase 18 m3 only WRITE_FAILED filled it, and three
 * consumers folded the sentence in afterwards by hand, identically, while the pure-C consumer
 * had to know to make the second call: a composed call the API could make and did not. */
/* Always autosaves the working copy afterwards, unlike `_load`/`_set_working`/`_edit`: a save-as
 * is an explicit write and the working copy records its new origin. Decided and asserted
 * (`studio_golden_test`); a `persist` parameter was tried and removed 2026-09-06. */
int rolltui_preset_store_save_as(RolltuiPresetStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);

/* The two paths. Both REPLACE `*out` — text out, rule 3(b), the same shape
 * `rolltui_preset_store_label` beside them already used. They took a `RolltuiPutFn` until
 * Phase 17 m4b, which is why every consumer had a lambda-and-append around them. */
void rolltui_preset_store_working_path(const RolltuiPresetStore* s, RolltuiStr* out);
void rolltui_preset_store_preset_path(const RolltuiPresetStore* s, const char* name, size_t len, RolltuiStr* out);

/* Mirrors `rolltui::ThemeLoadReport`/`PresetLoadReport`'s Theme-relevant fields, transparent
 * like `RolltuiThemeReport`/`RolltuiAppProfileReport` one file over — nothing about a
 * diagnostic list needs hiding, and nothing outside `rolltui_presets.c` ever writes one; a
 * caller only reads it after a parse call, then releases it. */
typedef struct RolltuiThemePresetReport {
  RolltuiStr error; /* non-empty: unusable */
  RolltuiStr* bad_values;   size_t bad_values_n,   bad_values_cap;   /* GROWING AMORTISED */
  RolltuiStr* unknown_keys; size_t unknown_keys_n, unknown_keys_cap; /* GROWING AMORTISED */
  RolltuiStr* notes;        size_t notes_n,        notes_cap;        /* GROWING AMORTISED */
  RolltuiThemeReport colours; /* the "colours" part's own report, verbatim */
} RolltuiThemePresetReport;

/* Frees everything and zeroes the struct — safe on an already-zeroed one and on repeated
 * calls, the same "reset, not just release" contract every report on this boundary states. */
void rolltui_theme_preset_report_release(RolltuiThemePresetReport* r);

/* A pure predicate over a "mode"/"depth" string — no context, because `valid_mode_setting`/
 * `valid_depth_setting` (Presets.hpp) are themselves pure over a `string_view` with nothing to
 * capture. `Presets.cpp` hands over a captureless-lambda-decayed function pointer, the same
 * bridge `PresetStore.hpp`'s own `domain_storage<D>()` already builds an entire domain
 * descriptor out of. */
typedef int (*RolltuiThemePresetValidFn)(const char* s, size_t len);

/* Parses a preset file's top level from an already-parsed tree: "name"/"preset" (must be a
 * string when present, else a bad value; the VALUE itself is the store's business, not read
 * here), "mode"/"depth" (checked with `mode_valid`/`depth_valid`; kept at whatever `out_mode`/
 * `out_depth` already held — this file sets both to "auto" first, matching `ThemePreset`'s own
 * member-initialisers — when the key is absent or fails its check), "colours" (required; a
 * bad "preset file must be a JSON object"/"needs a \"colours\" object" `report->error` when
 * `root` itself is not usable, checked BEFORE anything else), "layout" (a Phase 9 leftover:
 * not an error, a NOTE naming it — this format's own vocabulary to own, the same position
 * `rolltui_theme.h` takes for a theme file's structural keys). Unknown keys are reported, not
 * rejected. Colour validation runs at BOTH modes (dark then light) so a role wrong only in one
 * variant is still caught: `report->colours` is dark's report, plus light's bad values not
 * already in dark's — mirroring `theme_preset_from_json`'s own double load exactly, including
 * the asymmetry that only DARK's success/failure decides the return value.
 *
 * Returns 1 when `root` is a usable preset (`report` may still carry notes/bad_values/
 * unknown_keys — a usable file can still have problems), 0 when it is not (`report->error`
 * says which; `*out_colours` is NULL). `report` is reset by this call, as every report on this
 * boundary is. */
int rolltui_theme_preset_parse(const RolltuiJsonValue* root, const RolltuiThemeVocab* vocab,
                               RolltuiThemePresetValidFn mode_valid, RolltuiThemePresetValidFn depth_valid,
                               RolltuiStr* out_mode, RolltuiStr* out_depth, const RolltuiJsonValue** out_colours,
                               RolltuiThemePresetReport* report);

/* Builds a preset file's tree: {"name","mode","depth","colours"}. TAKES OWNERSHIP of
 * `colours` (folds it into the result directly, the same contract `rolltui_json_set` itself
 * has) — the caller has usually just built it fresh via `json::value_to_c` for this one call
 * and has no further use for it, so consuming it here is a clone fewer. OWNED; the caller
 * frees the result with `rolltui_json_free`. Never sets "preset": that key is
 * `to_json_with_origin`'s, one level up, not this format's own. */
RolltuiJsonValue* rolltui_theme_preset_to_json(RolltuiJsonValue* colours, const char* mode, size_t mode_len,
                                               const char* depth, size_t depth_len, const char* name,
                                               size_t name_len);

/* ---- the Theme domain ----------------------------------------------------------------------
 * ~~`rolltui::ThemePreset` (Presets.hpp) IS this struct~~ — **FALSE, and a segfault is what
 * found it (Phase 17 m3, 2026-09-05).** `ThemePreset` holds `std::string mode/depth`; this
 * holds `RolltuiStr`. They are two different structs that describe the same thing, and casting
 * a store's `void*` from one to the other crashes. "two strings, which is already all-C" was
 * true of the CONCEPT and false of the TYPE, and the sentence collapsed the two.
 *
 * ~~**The consequence is bigger than the wording.** There are two Theme preset DOMAINS in the
 * tree … the second has NO production caller — only six `presets_test` assertions that its
 * function pointers are non-NULL. Ported but unreachable … switching the stores over is
 * m2c's.~~ **DONE, AND THIS PARAGRAPH WAS THEN FALSE FOR A DAY (corrected 2026-09-05, Phase
 * 16 m6).** m2c did switch them: `Presets.hpp`, `PresetStore.hpp` and `rolltui::ThemePreset`
 * are deleted, there is no C++ header left in `rolltui/` at all, and **`rolltui_theme_preset_domain_init`
 * is now the only Theme domain there is** — `studio.cpp` and `src/frontends/TuiFrontend.cpp`
 * both build their store from it. So there is ONE domain, and this struct IS its value type.
 *
 * **WHY THE CORRECTION IS RECORDED RATHER THAN JUST MADE, and it is the same lesson one level
 * up.** The struck text told a reader that this API is unreachable and has no production
 * caller. The only reader who believes a public header over the call sites is one who cannot
 * see the call sites — which is exactly the non-C++ consumer this vocabulary exists for, and
 * exactly who `plan/phase-16.md` m6 built to stand in for. A stale comment about a struct's
 * identity is what produced the segfault struck above; a stale comment about its REACHABILITY
 * is the same failure aimed at whoever comes next. **A milestone that deletes a thing owns
 * every sentence that described it.** */
typedef struct RolltuiThemePresetValue {
  RolltuiJsonValue* colours ROLLTUI_DEFAULT(nullptr); /* OWNED */
  RolltuiStr mode;                                     /* "auto" | "dark" | "light" */
  RolltuiStr depth;                                    /* "auto" | "truecolor" | "256" | "16" | "mono" */
} RolltuiThemePresetValue;

void rolltui_theme_preset_value_release(RolltuiThemePresetValue* v); /* frees `colours`; zeroes */

/* Fills `out` with the Theme domain's mechanics — nothing here is a paraphrase of
 * `rolltui_theme_preset_parse`/`_parse_partial`, it is those functions with the walk they
 * already do. `vocab`/`mode_valid`/`depth_valid` are BORROWED for the process's life: a host
 * calls this once, at startup, with process-lifetime tables — the same assumption
 * `rolltui_windows_set_builtin_roles` already makes of ITS caller. */
void rolltui_theme_preset_domain_init(RolltuiPresetDomain* out, const RolltuiThemeVocab* vocab,
                                      RolltuiThemePresetValidFn mode_valid, RolltuiThemePresetValidFn depth_valid);

/* ---- the Layout domain ----------------------------------------------------------------------
 * The Value is `RolltuiLayout` itself (rolltui_layout.h): the shipped presets ARE the
 * built-ins, embedded once and read by both a host's `builtin_layout()`-shaped lookup and this
 * domain, so the two can never disagree — the same fact Presets.hpp already states of the
 * C++ path. */
typedef struct RolltuiLayoutPresetReport {
  RolltuiStr error; /* the PRESET-level error: a copy of `layout.error` on failure, or the
                     * mechanics' own ("no layout preset 'x' ...") */
  RolltuiLayoutReport layout; /* the file's own: error, unknown_keys, bad_values, notes */
  RolltuiStr* notes;          /* one per `layout.notes` entry, plus whatever the mechanics
                               * itself adds */
  size_t notes_n, notes_cap;
} RolltuiLayoutPresetReport;

void rolltui_layout_preset_report_release(RolltuiLayoutPresetReport* r); /* frees everything; zeroes */

/* `default_actions`/`_n` are BORROWED for the process's life, same as `hooks` — a host passes
 * `shipped_default_actions()`'s C form (the shipped "default" layout's own "actions" list). */
void rolltui_layout_preset_domain_init(RolltuiPresetDomain* out, const RolltuiLayoutHooks* hooks,
                                       const RolltuiLayoutAction* default_actions, size_t default_actions_n);

/* ---- the Bindings domain --------------------------------------------------------------------
 * The Value is `RolltuiBindings*` itself (rolltui_bindings.h) — already fully C, so this
 * domain's `clone`/`destroy`/`equal` are `rolltui_bindings_clone`/`_free`/`_equal` verbatim. */
typedef struct RolltuiBindingsPresetReport {
  RolltuiStr error; /* the PRESET-level error: a copy of `bindings.error` on failure, or the
                     * mechanics' own */
  RolltuiBindingsReport bindings; /* the file's own: unknown_actions/bad_chords/undeliverable/
                                   * conflicts/bad_values/unknown_keys (its "bindings" object) */
  RolltuiStr* unknown_keys; /* the PRESET file's own top-level keys other than "name" /
                             * "bindings" / "preset" — `rolltui_bindings_load_json` only ever
                             * looks at its "bindings" object, so this level's unknown keys are
                             * this domain's own to find */
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* notes; /* whatever the mechanics itself adds */
  size_t notes_n, notes_cap;
} RolltuiBindingsPresetReport;

void rolltui_bindings_preset_report_release(RolltuiBindingsPresetReport* r); /* frees everything; zeroes */

/* ---- IS THIS REPORT CLEAN, AND WHAT DOES IT SAY — per domain (Phase 17 m3) -----------------
 * `rolltui_preset_report_summary` above is the generic COMPOSER: it takes the pieces and joins
 * them in order. What it does not know is which pieces each domain has, so every caller wired
 * its own fields in — and `clean()` was re-derived outright, three times per host.
 *
 * Both hosts wrote all six independently in one session (roll's `TuiFrontend.hpp`, the
 * studio's `studio.cpp`), which is `rolltui.h` rule 5's tell: two consumers writing the same
 * thing means the API is wrong, not the consumers. Unlike the store wrappers around them —
 * which are marshalling, and a language-boundary cost this phase deliberately pushed onto
 * hosts — this is a JUDGEMENT about the library's own data ("does a missing role make a theme
 * preset unclean?"), and two hosts answering it separately is two answers waiting to differ.
 *
 * `_clean` returns 1 when the report has nothing to report. `_summary` APPENDS the same
 * sentence the composer would, with that domain's fields already wired. */
int rolltui_theme_preset_report_clean(const RolltuiThemePresetReport* r);
void rolltui_theme_preset_report_summary(const RolltuiThemePresetReport* r, RolltuiStr* out);
int rolltui_layout_preset_report_clean(const RolltuiLayoutPresetReport* r);
void rolltui_layout_preset_report_summary(const RolltuiLayoutPresetReport* r, RolltuiStr* out);
int rolltui_bindings_preset_report_clean(const RolltuiBindingsPresetReport* r);
void rolltui_bindings_preset_report_summary(const RolltuiBindingsPresetReport* r, RolltuiStr* out);

/* `is_library_scope`/`reason` are BORROWED for the process's life, the same two
 * callbacks `rolltui_bindings_load_json` already takes — this keeps a copy to hand over on
 * every call instead of threading them through the generic mechanics. */
void rolltui_bindings_preset_domain_init(RolltuiPresetDomain* out, RolltuiScopeFn is_library_scope, void* scope_ctx,
                                         RolltuiReasonFn reason,
                                         void* reason_ctx);

/* Which of the three a thing belongs to — a setting (`RolltuiPresetSettingSpec` below), or a
 * store. Distinct from `RolltuiPresetDomain` above (that struct is the MECHANICS for one
 * domain — parse/to_json/clone/...; this is a tag naming one of the library's three), hence
 * the `Id` suffix. The library's own closed set, like `Anchor` and `Border`: a host domain
 * built through `_init` has no id and needs none — a store knows its domain by its `kind`. */
typedef enum RolltuiPresetDomainId {
  ROLLTUI_PRESET_DOMAIN_THEME = 0,
  ROLLTUI_PRESET_DOMAIN_LAYOUT,
  ROLLTUI_PRESET_DOMAIN_BINDINGS,
} RolltuiPresetDomainId;

/* A BORROW of a static string literal: "theme" | "layout" | "bindings" — and, not by
 * coincidence, exactly the key that is each domain's own IDENTITY setting (`kSettings`
 * below) AND each library domain's `kind`: one spelling of the name, three readers. */
const char* rolltui_preset_domain_name(RolltuiPresetDomainId d, size_t* len);

RolltuiPresetDomain* rolltui_preset_domain(RolltuiPresetDomainId id);

typedef enum RolltuiPresetRung {
  ROLLTUI_PRESET_RUNG_FLAG = 0,
  ROLLTUI_PRESET_RUNG_ENV,
  ROLLTUI_PRESET_RUNG_WORKING,
  ROLLTUI_PRESET_RUNG_BUILTIN,
} RolltuiPresetRung;

/* A BORROW of a static string literal, never freed: "flag" | "environment" | "working copy" |
 * "built-in default". */
const char* rolltui_preset_rung_name(RolltuiPresetRung r, size_t* len);

/* THE WHOLE RULE (Presets.hpp): the first NON-EMPTY rung wins. An empty string at a rung means
 * "not given there". `*out_value`/`*out_value_len` BORROW whichever of the four input strings
 * won — never copied, never allocated, valid exactly as long as that one input buffer is (the
 * same window the caller's own four strings already have). */
void rolltui_preset_resolve_setting(const char* flag, size_t flag_len, const char* env, size_t env_len,
                                    const char* working, size_t working_len, const char* builtin,
                                    size_t builtin_len, const char** out_value, size_t* out_value_len,
                                    RolltuiPresetRung* out_rung);

/* A setting's value in a store's WORKING COPY, APPENDED to `out` (empty when this key is not
 * this domain's). The identity key — the store's own domain's `kind`, "theme"/"layout"/
 * "bindings" — answers with the origin; a Theme store additionally answers "theme_mode" and
 * "color_depth". Until Phase 18 m3 this took a `RolltuiPresetDomainId` beside the store,
 * with the comment "the caller knows, and the store does not carry its own tag" — it does:
 * `kind` IS the name, and the id was a second spelling of it that every caller had to keep
 * in step (roll, once per store class; the C consumer, at every call). */
void rolltui_preset_working_value(const RolltuiPresetStore* s, const char* key, size_t key_len, RolltuiStr* out);

/* One row of `kSettings` (Presets.hpp): a setting's key, which domain/store it belongs to, its
 * environment-variable suffix ("THEME" joined to a host's own prefix), its built-in default,
 * and help text for its legal values. BORROWED fields throughout — every string is a literal
 * in the table below, alive for the process's whole life. */
typedef struct RolltuiPresetSettingSpec {
  const char* key;
  size_t key_len;
  RolltuiPresetDomainId domain;
  const char* env_suffix;
  size_t env_suffix_len;
  const char* builtin;
  size_t builtin_len;
  const char* values; /* help text, e.g. "auto | dark | light" */
  size_t values_len;
} RolltuiPresetSettingSpec;

size_t rolltui_preset_settings_count(void);
/* BORROW, table order ("theme", "layout", "theme_mode", "color_depth", "bindings" — the order
 * a listing offers them in), valid for the process's whole life. */
const RolltuiPresetSettingSpec* rolltui_preset_settings_at(size_t i);
/* The row named `key`, as an INDEX into the table above (`rolltui_preset_settings_at`) rather
 * than a pointer — the shape a caller whose OWN copy of this table is a different array
 * (`Presets.cpp`'s `kSettings`, built from this one row for row) needs to find the matching
 * row without a second string comparison. -1: `key` is not a known setting. */
int rolltui_preset_setting_index(const char* key, size_t len);

#ifdef __cplusplus
/* The one method that cannot be inline in the struct: it calls a function declared after it.
 * Same placement, and same reason, as `RolltuiStr::~RolltuiStr` in `rolltui_str.h`. */
inline RolltuiPresetList::~RolltuiPresetList() { rolltui_preset_list_release(this); }

#endif

/* ========================================================================================
 * render — the grid to bytes
 * ======================================================================================== */
/* The whole frame, from a cleared screen. Appends to `out`; never clears it. */
void rolltui_render_full(const RolltuiFrame* next, unsigned char depth, RolltuiStr* out);


/* The frame as plain text, one row per line with trailing spaces trimmed. The golden-frame
 * harness is the caller; no escapes, no styles. */
void rolltui_frame_to_text(const RolltuiFrame* f, RolltuiStr* out);


/* ========================================================================================
 * swap — the double buffer, the newest entry point
 * ======================================================================================== */
typedef struct RolltuiSwap RolltuiSwap;

/* Both frames at `w` x `h`, filled with `fill`. Never returns NULL: an allocation failure
 * aborts through `rolltui_mem_alloc`, which is the library's stated answer. */
RolltuiSwap* rolltui_swap_new(int w, int h, RolltuiStyle fill);
void rolltui_swap_free(RolltuiSwap* s);

/* Resets the back frame to `w` x `h` and LENDS it for drawing. Valid until the next `begin`
 * or `present`; the caller never frees it. A size change here is safe and is handled by
 * `rolltui_render_diff` at present time, so a caller does not have to notice one. */
RolltuiFrame* rolltui_swap_begin(RolltuiSwap* s, int w, int h, RolltuiStyle fill);

/* Diffs the drawn frame against the previous one, APPENDS the bytes to `out`, and swaps.
 * After this the drawn frame is the baseline and the other is the next `begin`'s target.
 * Appends nothing when nothing changed and the cursor did not move. */
void rolltui_swap_present(RolltuiSwap* s, unsigned char depth, RolltuiStr* out);

/* "Repaint whole at the next present." THE HOST'S POLICY, and the only half of the old
 * `have_prev` that was ever a host's: a new layout, a new palette, an explicit repaint. A
 * size change needs no call — see the header comment. */
void rolltui_swap_invalidate(RolltuiSwap* s);

/* The frame most recently presented, for a caller that needs to read it back — the golden
 * harness and `poll_timeout_ms` both do. Borrowed, valid until the next `present`. */
const RolltuiFrame* rolltui_swap_front(const RolltuiSwap* s);


/* ========================================================================================
 * terminal — the one fd
 * ======================================================================================== */
/* ---- options, defined once and compiled by both languages ------------------------------ */
/* The attribute bits are `unsigned char` and not `bool` for `rolltui_style.h`'s reason: C's
 * `_Bool` and C++'s `bool` are the same byte on every toolchain this will ever see, and that
 * is exactly the "layout-compatible by fiat" this project keeps being burned by — one type
 * in both languages, nothing to assume. */
typedef struct RolltuiTerminalOptions {
  unsigned char alt_screen ROLLTUI_DEFAULT(1);
  unsigned char mouse ROLLTUI_DEFAULT(1);        /* SGR 1006 + button + drag reporting */
  unsigned char bracketed_paste ROLLTUI_DEFAULT(1);
  unsigned char hide_cursor ROLLTUI_DEFAULT(1);
  unsigned char handle_signals ROLLTUI_DEFAULT(1); /* restore-and-reraise on INT/TERM/HUP/QUIT */
} RolltuiTerminalOptions;

typedef struct RolltuiTerminal RolltuiTerminal;

/* ---- lifetime --------------------------------------------------------------------------- */
/* OWNED by the caller. Enters immediately (raw mode, alt screen, the rest of `opts`) and
 * negotiates the keyboard protocol before returning, exactly as the C++ constructor did.
 * Never returns NULL: an allocation failure aborts inside rolltui::mem. */
RolltuiTerminal* rolltui_terminal_new(int in_fd, int out_fd, RolltuiTerminalOptions opts);
/* Restores the terminal (see rolltui_terminal_restore_now) and releases everything `t` holds. */
void rolltui_terminal_free(RolltuiTerminal* t);

/* ---- geometry --------------------------------------------------------------------------- */
int rolltui_terminal_is_tty(const RolltuiTerminal* t);
int rolltui_terminal_width(const RolltuiTerminal* t);
int rolltui_terminal_height(const RolltuiTerminal* t);

/* ---- events ----------------------------------------------------------------------------- */
/* The same three kinds `rolltui_keys.h` defines, plus RESIZE — see rule 4 above. */
#define ROLLTUI_TERM_EVENT_KEY ROLLTUI_EVENT_KEY
#define ROLLTUI_TERM_EVENT_MOUSE ROLLTUI_EVENT_MOUSE
#define ROLLTUI_TERM_EVENT_PASTE ROLLTUI_EVENT_PASTE
#define ROLLTUI_TERM_EVENT_RESIZE 3

/* ONE event. `text` is a BORROW valid only for the `emit` call (rule 3): an Unknown key's
 * raw bytes (kind KEY, key UNKNOWN) or a paste's contents (kind PASTE). NULL otherwise.
 * `w`/`h` are set only for kind RESIZE. */
typedef struct RolltuiTermEvent {
  unsigned char kind;
  RolltuiChord key;
  RolltuiMouseEvent mouse;
  const char* text;
  size_t text_len;
  int w, h;
} RolltuiTermEvent;

/* Called once per event, in order. The C++ side appends to its `std::vector<Event>`, the
 * same shape `RolltuiEventFn` already has one layer down. */
typedef void (*RolltuiTermEventFn)(void* ctx, const RolltuiTermEvent* e);

/* Waits up to timeout_ms (-1: forever) for input, a resize or rolltui_terminal_wake(); reports
 * whatever decoded (possibly nothing). A lone ESC that nothing follows within the timeout is
 * delivered as Escape. Bytes left over from a `negotiate_keyboard`/`query_background` call
 * that ran before this one are delivered first, before anything this call reads itself. */
void rolltui_terminal_poll(RolltuiTerminal* t, int timeout_ms, RolltuiTermEventFn emit, void* ctx);

/* Makes a blocked poll() return now, with whatever events are pending (possibly none).
 * Thread-safe and async-signal-safe: one byte on the self-pipe. A frontend whose view
 * changes on another thread uses it instead of a short poll timeout. */
void rolltui_terminal_wake(RolltuiTerminal* t);

/* Writes every byte (loops on partial writes and EINTR). */
void rolltui_terminal_write(RolltuiTerminal* t, const char* bytes, size_t len);


/* ---- background colour (milestone 11's light/dark auto-detect) -------------------------- */
/* Asks the terminal for its background colour (OSC 11) and waits up to timeout_ms for the
 * reply, writing it to `*out`. Returns 0 on a pipe, on no answer in time (a terminal that
 * does not implement OSC 11 sends nothing), or on an unparseable answer — the caller treats
 * every 0 as "dark". Bytes that arrive and are not the reply (a user already typing) are
 * kept and delivered by the next `rolltui_terminal_poll`; nothing is lost. Call before the
 * event loop, once. */
int rolltui_terminal_query_background(RolltuiTerminal* t, int timeout_ms, RolltuiStyleColor* out);



/* ========================================================================================
 * theme_analysis [TOOL-FACING] — the studio and the theme editor check themes; roll never does
 * ======================================================================================== */
/* ---- the three colour spaces, defined ONCE and compiled by both languages ------------- */
/* `rolltui::Lin`, `rolltui::OkLab` and `rolltui::OkLch` ARE these structs (ThemeAnalysis.hpp
 * aliases them), the same move Phase 14 m2 made for `Color`/`Style`. Linear sRGB, OKLab and
 * OKLCH are each three doubles with a defaulted-to-zero value — no methods, because nothing
 * in either language ever called one. */
typedef struct RolltuiLin {
  double r ROLLTUI_DEFAULT(0), g ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0); /* linear sRGB, 0..1 */
} RolltuiLin;

typedef struct RolltuiOkLab {
  double L ROLLTUI_DEFAULT(0), a ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0);
} RolltuiOkLab;

typedef struct RolltuiOkLch {
  double L ROLLTUI_DEFAULT(0), C ROLLTUI_DEFAULT(0), h ROLLTUI_DEFAULT(0); /* h in degrees, [0, 360) */
} RolltuiOkLch;

/* ---- colour-vision-deficiency type, as a byte (the same order as `rolltui::Cvd`) ------- */
#define ROLLTUI_CVD_PROTANOPIA 0
#define ROLLTUI_CVD_DEUTERANOPIA 1
#define ROLLTUI_CVD_TRITANOPIA 2
#define ROLLTUI_CVD_COUNT 3


/* 1 on success, 0 for `Color::none()` (the terminal's own colour — unknown, not assumed). */
int rolltui_to_linear(RolltuiStyleColor c, RolltuiLin* out);
/* Clamped, encoded to an Rgb colour. */




/* ---- badges: a fixed, closed set of 13 names — this module's OWN vocabulary, never a
 * Role's, so nothing below needs a vocab table to print or check one. --------------------- */
typedef struct RolltuiBadges {
  unsigned char dark ROLLTUI_DEFAULT(0), light ROLLTUI_DEFAULT(0), high_contrast ROLLTUI_DEFAULT(0),
      readable ROLLTUI_DEFAULT(0), cvd_safe ROLLTUI_DEFAULT(0);
  unsigned char protan_safe ROLLTUI_DEFAULT(0), deutan_safe ROLLTUI_DEFAULT(0), tritan_safe ROLLTUI_DEFAULT(0);
  unsigned char mono ROLLTUI_DEFAULT(0), safe_16 ROLLTUI_DEFAULT(0), safe_256 ROLLTUI_DEFAULT(0),
      transparent ROLLTUI_DEFAULT(0), attribute_redundant ROLLTUI_DEFAULT(0);
} RolltuiBadges;

/* A growing array of strings (rolltui_alloc.h strategy 2, GROWING AMORTISED) — this file's
 * one shape for "a list of short diagnostic messages": a report's claimed-badge failures.
 * `rolltui::Badges` is `using Badges = RolltuiBadges;` (ONE DEFINITION, the `Lin`/`Style`
 * move) — see this header's top comment for why `RoleCheck`/`PairCheck`/`Fix` are not. */
typedef struct RolltuiStrArray {
  RolltuiStr* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0), cap ROLLTUI_DEFAULT(0);
} RolltuiStrArray;
/* Frees every element and the array; zeroes. Safe on a zeroed array and on repeated calls. */
void rolltui_str_array_release(RolltuiStrArray* a);

/* Resets `out` first. Appends the names of the badges that are set, in the fixed order
 * `rolltui::badge_names` always used ("dark", "light", "high-contrast", "readable",
 * "cvd-safe", "protan-safe", "deutan-safe", "tritan-safe", "mono", "16-safe", "256-safe",
 * "transparent", "attribute-redundant"). */
void rolltui_badge_names(const RolltuiBadges* b, RolltuiStrArray* out);
/* 1 iff `name` is one of `b`'s set badges. */

/* ---- thresholds. `rolltui::kReadableRatio` etc. are `= ROLLTUI_*` aliases of these, so the
 * number is written down once. ------------------------------------------------------------ */
#define ROLLTUI_READABLE_RATIO 4.5
#define ROLLTUI_HIGH_CONTRAST_RATIO 7.0
#define ROLLTUI_DISTINCT_DELTA_E 0.08

/* The must-differ pairs' COUNT. The pairs themselves are a LIBRARY RULE, closed on purpose
 * (Phase 18 m1 — the decision, its reason and every pair's justification are written at the
 * table, `kMustDiffer` in rolltui_theme_analysis.c). Only the count is a constant here because
 * only the count is a caller's business: it sizes the `out_pairs` array `rolltui_theme_analyse`
 * fills, and the pairs come back IN it (`RolltuiPairCheck.a`/`.b`), which is how a theme author
 * sees the rule — by analysing a theme, never by editing the rule. */
#define ROLLTUI_MUST_DIFFER_COUNT 11
size_t rolltui_must_differ_count(void); /* returns ROLLTUI_MUST_DIFFER_COUNT */

/* ---- the per-role / per-pair check ------------------------------------------------------- */
typedef struct RolltuiRoleCheck {
  unsigned char role ROLLTUI_DEFAULT(0);  /* always `i` for out_roles[i] — positional, not looked up */
  unsigned char text ROLLTUI_DEFAULT(1);  /* is this role's fg drawn as text? (counts for readable/high) */
  double wcag ROLLTUI_DEFAULT(0), apca ROLLTUI_DEFAULT(0); /* meaningless when `unknown` */
  RolltuiStyleColor fg, bg;                /* measured colours (bg: the role's own, else the theme's background) */
  unsigned char unknown ROLLTUI_DEFAULT(0); /* depends on the terminal: a measured colour was None */
  unsigned char readable ROLLTUI_DEFAULT(0), high ROLLTUI_DEFAULT(0);
} RolltuiRoleCheck;

typedef struct RolltuiPairCheck {
  unsigned char a ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0); /* the pair's OWN role ordinals, not positional */
  double delta ROLLTUI_DEFAULT(0);          /* meaningless when `unknown` */
  double delta_cvd[3];                      /* per ROLLTUI_CVD_*; meaningless when `unknown` */
  unsigned char unknown ROLLTUI_DEFAULT(0);
  unsigned char distinct ROLLTUI_DEFAULT(0), cvd_distinct ROLLTUI_DEFAULT(0);
  unsigned char attribute_redundant ROLLTUI_DEFAULT(0);
  unsigned char collapses_16 ROLLTUI_DEFAULT(0), collapses_256 ROLLTUI_DEFAULT(0);
} RolltuiPairCheck;

/* CALLER-FILLED: out_roles[0..role_count), out_pairs[0..rolltui_must_differ_count()). Both
 * arrays and `*out_badges` are written positionally in full on success. Returns 0 (nothing
 * written) when `role_count` does not match this file's own role table (the same defensive
 * shape `rolltui_theme_builtin_fill` already takes). Mirrors `rolltui::analyse` exactly,
 * MINUS the notes — see this section's top comment for why those are the consumer's to
 * build from the `unknown`/`text`/`readable` flags and `*out_badges` this already returns. */
int rolltui_theme_analyse(const RolltuiStyle* styles, size_t role_count, RolltuiRoleCheck* out_roles,
                          RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges);

/* Composes the full report exactly as `rolltui::report_text` did: "badges: ...\n", each of
 * `notes` as "note: ...\n" (the caller's own, already built; see this section's top comment
 * for why those live in the consumer), a blank line and a line per TEXT
 * role, a blank line and a line per pair. APPENDS to `*out` (rolltui.h's UNBOUNDED text
 * shape) rather than clearing it first — a fresh caller passes a zero-initialised `RolltuiStr`. */
void rolltui_theme_report_text(const RolltuiRoleCheck* roles, size_t role_count, const RolltuiPairCheck* pairs,
                               size_t pair_count, const RolltuiBadges* badges, const RolltuiStr* notes,
                               size_t notes_n, const RolltuiThemeVocab* vocab, RolltuiStr* out);

/* A theme's claimed badges ("meta": {"badges": [...]}) against the computed ones: appends
 * each claim that does NOT hold, in claim order, to `out` (RESET first). `meta` may be NULL
 * or lack a "badges" array (or a "badges" that is not an array) — either way, nothing is
 * appended. */
void rolltui_check_claims(const RolltuiJsonValue* meta, const RolltuiBadges* badges, RolltuiStrArray* out);

/* ---- auto-fix proposals ------------------------------------------------------------------ */
typedef struct RolltuiFix {
  unsigned char role ROLLTUI_DEFAULT(0);
  RolltuiStyle before, after;
  RolltuiStr what;   /* OWNED — e.g. "md_link fg: contrast 3.1 -> 4.6" */
  double before_value ROLLTUI_DEFAULT(0), after_value ROLLTUI_DEFAULT(0);
} RolltuiFix;
/* Frees `what`, zeroes. Safe on a zeroed `RolltuiFix` and on repeated calls. */


/* A growing array of `RolltuiFix` (GROWING AMORTISED); each element owns its own `what`. */
typedef struct RolltuiFixArray {
  RolltuiFix* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0), cap ROLLTUI_DEFAULT(0);
} RolltuiFixArray;
/* Releases every element's `what`, then the array; zeroes. Safe on a zeroed array and on
 * repeated calls. */
void rolltui_fix_array_release(RolltuiFixArray* a);

/* Every failing role and pair, in report order (`rolltui_theme_analyse`'s own role and pair
 * order): a `rolltui_fix_contrast` for each TEXT role that is known and not yet readable, a
 * `rolltui_fix_confusable` for each pair that is known, not already (distinct AND
 * cvd_distinct), and not already attribute-redundant (the fix of last resort, applied). A
 * fix that turns out unavailable for a qualifying role/pair (e.g. an unknown colour slipping
 * through) is simply not appended. `out` is RESET first. */
void rolltui_propose_fixes(const RolltuiStyle* styles, size_t role_count, const RolltuiThemeVocab* vocab,
                           RolltuiFixArray* out);

/* `styles[role] = *after`. A no-op when `role` is out of range. Mirrors
 * `rolltui::apply_fix` (`theme.style(fix.role) = fix.after`) exactly. */
void rolltui_apply_fix(RolltuiStyle* styles, size_t role_count, unsigned char role, const RolltuiStyle* after);


/* ========================================================================================
 * theme_gen [TOOL-FACING] — the theme generator is the editor's
 * ======================================================================================== */
/* ---- the ruleset, as a byte (the same order as `rolltui::Ruleset`) --------------------- */
#define ROLLTUI_RULESET_ANALOGOUS 0
#define ROLLTUI_RULESET_COMPLEMENTARY 1
#define ROLLTUI_RULESET_TRIADIC 2
#define ROLLTUI_RULESET_TETRADIC 3
#define ROLLTUI_RULESET_MONOCHROME 4
#define ROLLTUI_RULESET_PASTEL 5
#define ROLLTUI_RULESET_NEON 6
#define ROLLTUI_RULESET_EARTH 7
#define ROLLTUI_RULESET_COUNT 8

/* A BORROW of a string literal; never NULL, `*len` 0 for an out-of-range ruleset. `len`
 * may be NULL. */
const char* rolltui_ruleset_name(unsigned char ruleset, size_t* len);
/* 1 and `*out` set on a match, 0 (leaving `*out` untouched) otherwise. */
int rolltui_ruleset_from_name(const char* name, size_t len, unsigned char* out);

/* ---- generate() (Phase 17 m5) ------------------------------------------------------------
 *
 * Builds a whole theme positionally into `out_styles[0..role_count)` (CALLER-FILLED, the
 * same convention `rolltui_theme_builtin_fill` already uses) and runs the repair loop
 * (`rolltui_theme_analyse` / `rolltui_propose_fixes` / `rolltui_apply_fix`,
 * `rolltui_theme_analysis.h`) until the promised badges hold or it gives up. Mirrors
 * `rolltui::generate` exactly (same hue/lightness picks per ruleset, same jitter/chance
 * draws off the same PRNG sequence, same repair loop shape), so the SAME (seed, ruleset,
 * chaos) still yields the SAME theme — `theme_gen_test.cpp`'s determinism check is the
 * oracle for this.
 *
 * `has_dark`/`dark_value` stand in for `GenOptions::dark` (a `std::optional<bool>` — one bit
 * needs no struct): `has_dark` 0 means "let the seed decide" (nullopt), matching
 * `opts.dark ? *opts.dark : rng.unit() < 0.6`. `max_repair_passes` is `GenOptions`'s field of
 * the same name verbatim. `vocab` is forwarded to `rolltui_propose_fixes` only — see this
 * header's top comment for why `generate()`'s own "broken" list is not built here.
 *
 * Returns 0 (nothing written) when `role_count` does not match this file's own role table
 * (the same defensive shape `rolltui_theme_builtin_fill` already takes). On success:
 *   out_styles[0..role_count)   the generated theme's styles, CALLER-FILLED
 *   *out_name                   "gen-<ruleset>-<seed>-<chaos>" (OWNED — free with
 *                                `rolltui_str_free`, or hand it straight to a `Theme::name`)
 *   *out_meta                   a fresh OWNED tree: {"generator": {"ruleset","seed","chaos"},
 *                                "badges": [...]} (free with `rolltui_json_free`, or adopt it
 *                                into `Theme::meta` directly)
 *   *out_repairs                fixes applied by the repair loop
 *   out_roles / out_pairs       the FINAL, post-repair `rolltui_theme_analyse` snapshot —
 *                                sized exactly as that function's own out-params
 *                                (role_count, `rolltui_must_differ_count()`) — for the
 *                                shim's "broken" list
 *   *out_badges                 the final computed badges (same as `out_meta`'s "badges",
 *                                as bits rather than names) */
int rolltui_theme_generate(uint64_t seed, unsigned char ruleset, double chaos, int has_dark, int dark_value,
                           int max_repair_passes, const RolltuiThemeVocab* vocab, RolltuiStyle* out_styles,
                           size_t role_count, RolltuiStr* out_name, RolltuiJsonValue** out_meta, int* out_repairs,
                           RolltuiRoleCheck* out_roles, RolltuiPairCheck* out_pairs, RolltuiBadges* out_badges);


/* ========================================================================================
 * undo [TOOL-FACING] — the editors' undo stack
 * ======================================================================================== */
/* Releases one snapshot the stack no longer holds — T's own destructor, generated once
 * per T by the C++ side (rolltui/tools/undo_stack.hpp). Fixed for the life of a stack:
 * one instance is always over one T, so this is supplied once at rolltui_undo_new and
 * never again. */
typedef void (*RolltuiUndoFreeFn)(void* snapshot);

typedef struct RolltuiUndoStack RolltuiUndoStack;

/* Starts EMPTY — nothing to undo or redo until reset() seeds a baseline, the same state
 * `rolltui::UndoStack<T>`'s default constructor left. `limit` bounds how many snapshots
 * are kept; past it, commit() drops the OLDEST rather than refusing the newest (0 is a
 * degenerate but well-defined bound: every commit evicts back down to one entry). */
RolltuiUndoStack* rolltui_undo_new(size_t limit, RolltuiUndoFreeFn free_fn);
void rolltui_undo_free(RolltuiUndoStack* u); /* no-op on NULL (rule 1) */

/* Restarts from a new baseline (a load, a reset): every existing snapshot is freed and
 * `baseline` becomes the sole entry, at position 0. TAKES OWNERSHIP of `baseline`. */
void rolltui_undo_reset(RolltuiUndoStack* u, void* baseline);
/* Records a new current value: the redo branch (if any) is freed and dropped, and the
 * OLDEST entry is freed too if this push carries the stack past its limit. TAKES
 * OWNERSHIP of `value`. */
void rolltui_undo_commit(RolltuiUndoStack* u, void* value);
/* Overwrites the CURRENT snapshot in place: no new step, the redo branch untouched (the
 * caret-move case `rolltui/Undo.hpp` was written for — a move must not become its own
 * undo step, yet must be what the next real edit's "before" snapshot restores to). TAKES
 * OWNERSHIP of `value`; the snapshot it replaces is freed. */
void rolltui_undo_replace_current(RolltuiUndoStack* u, void* value);

int rolltui_undo_undo(RolltuiUndoStack* u); /* 0/1: false at the baseline */
int rolltui_undo_redo(RolltuiUndoStack* u); /* 0/1: false with nothing committed since */
int rolltui_undo_can_undo(const RolltuiUndoStack* u);
int rolltui_undo_can_redo(const RolltuiUndoStack* u);
size_t rolltui_undo_undo_depth(const RolltuiUndoStack* u); /* steps back to the baseline */
size_t rolltui_undo_redo_depth(const RolltuiUndoStack* u); /* steps forward to the newest commit */
int rolltui_undo_empty(const RolltuiUndoStack* u);         /* true only before the first reset() */

/* The snapshot at the current position — the value before the last commit, once undo()
 * has walked back. BORROWED (rule 3c): valid until the next reset/commit/
 * replace_current/undo/redo/free call on THIS stack, and NULL only when empty(). */
const void* rolltui_undo_current(const RolltuiUndoStack* u);


/* ========================================================================================
 * widget_kinds — the built-in kinds and their registration
 * ======================================================================================== */
/* THE ROLE BYTES these kinds draw with, handed over ONCE at registration — this file names
 * no role, the same rule every other C header in this port states for itself. Role ordinals
 * are process-wide constants (`rolltui/Style.hpp`), so a value copied in at construction
 * never goes stale. */
typedef struct RolltuiBuiltinRoles {
  unsigned char text, text_muted, error, scroll_marker, label, value;
  unsigned char input_text, input_selection, input_placeholder;
} RolltuiBuiltinRoles;

/* THE SIX ACTION NAMES the transcript SCOPE's scroll keys use ("this file knows the rule and
 * none of the words" — the same trade `rolltui_transcript.h`'s `RolltuiTranscriptActions`
 * makes). `rolltui::scroll_by_action` (Widgets.hpp, unchanged and still used by two hosts
 * directly) hardcodes the identical six strings; this is the same one-file duplication
 * `Input.cpp`'s `kActions` and `Transcript.cpp`'s own action table already are, not a new
 * one — the alternative (an action name crossing the C boundary) is what `rolltui_bindings.h`
 * says not to do. */
typedef struct RolltuiScrollTextActions {
  const char *line_up, *line_down, *page_up, *page_down, *top, *bottom;
} RolltuiScrollTextActions;

const RolltuiBuiltinRoles* rolltui_windows_builtin_roles(const RolltuiWindows* w);
/* The LIBRARY'S OWN six (Phase 17 m2a) — the same list `RolltuiTranscriptActions` draws from,
 * minus the five that need a transcript. BORROWS static storage. */
const RolltuiScrollTextActions* rolltui_scroll_text_default_actions(void);

/* THE FIVE VOCABULARIES AND THE KINDS, IN ONE CALL (Phase 17 m2a) — every setter in this file
 * with the library's own defaults, then `rolltui_widget_kinds_register`, in the order that
 * function requires. This is what makes a bare `rolltui_windows_new()` usable by a pure-C
 * host: before it, the five setters were called by `rolltui::Windows`' C++ constructor and a C
 * caller got a table with no kinds and NULL action names. A host with its own words calls the
 * setters after; this is a default, not a policy. Idempotent; NULL is a no-op. */
void rolltui_windows_set_library_defaults(RolltuiWindows* w);

/* line_up/down, page_up/down, top/bottom applied to a `page`-row view of `total` lines, with
 * `*top` clamped to [0, total-page]. 0 when the chord is not one of the six. */
int rolltui_scroll_by_action(const RolltuiScrollTextActions* actions, const RolltuiBindings* bindings,
                             const RolltuiChord* k, int page, int total, int* top);

/* The input window's sizing rule: the cap is HALF the parent's extent less the border rows, at
 * least 1; the rows are the text's capped at that, plus one for a note that cannot sit beside a
 * single row. */
int rolltui_input_max_rows(int parent_extent, int border_rows);
int rolltui_input_window_rows(int text_rows, int end_col, int note_width, int width, int max_rows);

/* The key list for ONE scope, appended as "<indent><chord-or-(unbound)><pad><description>\n"
 * rows, the chord column aligned to the widest (capped at 22, minimum column 12).
 * Undeliverable chords are left out, which is why this takes the live table rather than names.
 * `actions`/`action_lens`/`actions_n` name the rows and their order when non-empty; otherwise
 * every action of `scope` in table order. `indent` prefixes every row (the help document uses
 * two spaces; `rolltui::help_lines` uses none). */
void rolltui_help_scope_lines(const RolltuiBindings* b, const char* scope, size_t slen, const char* const* actions,
                              const size_t* action_lens, size_t actions_n, const char* indent, size_t indent_len,
                              RolltuiStr* out);

/* The whole help document: `lead`, then one "<scope>:\n" section per scope with that scope's
 * rows under it, then `note`. APPENDS to `out`. */
void rolltui_help_document(const RolltuiBindings* b, const char* lead, size_t lead_len, const char* const* scopes,
                           const size_t* scope_lens, size_t scopes_n, const char* note, size_t note_len,
                           RolltuiStr* out);

/* Shared by the input plugin's own `handle` slot and by a host asking for the action back
 * (`Windows::input_event`): handle `e` against the live bindings, and on Submit either
 * clear-and-push-history or keep the text (`rolltui_windows_on_submit` says which), then call
 * whatever is bound to `source`. Returns one of ROLLTUI_INPUT_IGNORED/HANDLED/SUBMIT/EOF. */
int rolltui_input_kind_process_event(RolltuiInput* ed, RolltuiWindows* w, const char* source, size_t source_len,
                                      const RolltuiEvent* e);

/* The two ints a transcript's code-block folding needs — `Windows::set_code_fold`'s own,
 * mirrored to the boundary so the transcript kind below can read them at layout time. */
typedef struct RolltuiCodeFold {
  int fold_over_lines, cap_lines;
} RolltuiCodeFold;
void rolltui_windows_set_code_fold(RolltuiWindows* w, const RolltuiCodeFold* c);

const RolltuiMenuRoles* rolltui_windows_menu_roles(const RolltuiWindows* w);


/* ========================================================================================
 * wrap — the wrap engine roll draws with
 * ======================================================================================== */
/* `ambiguous_wide` is `unsigned char` and not `bool` for m2's reason (rolltui_style.h): C's
 * `_Bool` and C++'s `bool` are the same byte on every toolchain this will meet, and that is
 * exactly the layout-compatible-by-fiat this project keeps being burned by. The field order
 * is the one the C++ struct had, because two call sites write it as a designated initializer
 * and those are order-sensitive. */
typedef struct RolltuiWrapOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int tab_width ROLLTUI_DEFAULT(8);
  int first_indent ROLLTUI_DEFAULT(0);    /* cells before the first line */
  int hanging_indent ROLLTUI_DEFAULT(0);  /* cells before every subsequent line */
} RolltuiWrapOptions;

/* One drawn grapheme cluster of one line. */
typedef struct RolltuiWrapGrapheme {
  size_t offset;         /* into the line's text */
  size_t length;         /* bytes in the line's text */
  size_t source_offset;  /* byte offset in the wrap input (a tab's, for each of its spaces) */
  int width;             /* cells */
  unsigned char space;   /* U+0020 or an expanded tab: droppable at a soft break */
} RolltuiWrapGrapheme;

typedef struct RolltuiWrapLines RolltuiWrapLines;

/* ---- lifetime ------------------------------------------------------------------------ */
/* OWNED by the caller. `new` never returns NULL: an allocation failure aborts inside
 * rolltui::mem, because there is nothing useful to do with a half-built wrap. */
RolltuiWrapLines* rolltui_wrap_new(void);
void rolltui_wrap_free(RolltuiWrapLines* w);
/* Drops the LIVE LINES and keeps every buffer, so a handle that is wrapped into repeatedly
 * allocates nothing after its first few calls. This is what `Scratch` calls on acquire and
 * release, and it is why a steady frame is still zero with this implementation linked. */

/* ---- the engine ----------------------------------------------------------------------- */
/* Wraps `len` bytes of UTF-8 to `width` cells, into `w`, reusing everything `w` holds.
 * `width <= 0` is legal and draws nothing (one empty line per paragraph); empty input is one
 * empty line. The rules are Wrap.hpp's. */
void rolltui_wrap(RolltuiWrapLines* w, const char* utf8, size_t len, int width, RolltuiWrapOptions opt);


/* ---- reading the lines ----------------------------------------------------------------- */
size_t rolltui_wrap_line_count(const RolltuiWrapLines* w);
/* Line `i` as BORROWS into `w` (see rule 2 above). `text` is NOT NUL-terminated — `text_len`
 * is the length, and a zero-length line gives a valid non-NULL pointer. `hard` receives 1
 * when the line was ended by a mandatory break or by the end of the text. */
void rolltui_wrap_line(const RolltuiWrapLines* w, size_t i, const char** text, size_t* text_len,
                       const RolltuiWrapGrapheme** graphemes, size_t* grapheme_count,
                       int* width, int* indent, int* hard);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_H */
