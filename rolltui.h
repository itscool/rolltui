#ifndef ROLLTUI_H
#define ROLLTUI_H
/*
 * rolltui.h — the library's one public header: every public type is defined here, every public function declared here, and the
 * vocabulary those signatures speak (roles, keys, caps, enums) lives here. `#include "rolltui/rolltui.h"` is the whole API.
 *
 * The headers under `rolltui/c/` are internal: they include this file first and add what the library's own `.c` files need. Don't
 * include one from a host; a suite that tests an internal names it and is listed in CMake's opt-in list (`public_header_test`
 * asserts no other consumer does). What is public, and why, is `tests/api_classes.inc` (one class per function, held to measured
 * reach); who each function is for is `tests/api_roles.inc` — WHO a function is for is that data, tracked independently of WHERE
 * its declaration sits.
 *
 * LAYOUT. Four books, in dependency order: CORE (the primitives everything else is built from — text, geometry, colour, the cell
 * grid, JSON, Unicode, wrapping), CONFIGURATION (the four user/host-authored file types — theme, bindings, layout, menu — and the
 * stores that manage them), WIDGETS (what actually renders: the built-in widget kinds and the machinery — effects, markdown,
 * diff — that supports them), RUNTIME (the terminal, the double buffer, the run loop, the session that ties a screen together).
 * Each book is MODULE-MAJOR: a module's type and every function that takes it sit adjacent, in the same place, so "where do I look
 * for menus" is answered by one banner rather than two banners two thousand lines apart. A module never needs a second home for
 * ordering reasons alone — C's "a type before the functions that take it" is satisfied by declaring a module right after every
 * OTHER module whose type it uses (an opaque handle, `typedef struct X X;`, needs no such ordering and is forward-declared once,
 * early, for exactly this reason).
 *
 * THE RULES EVERY DECLARATION OBEYS
 *   1. HANDLES come in pairs, `rolltui_x_new` / `rolltui_x_free`, and `free` is a no-op on NULL. Call `rolltui_shutdown()` at the
 *      end and assert `live_bytes == 0 && live_blocks == 0` (`rolltui_mem_stats`) to catch a missed release.
 *   2. RETURNS. No `extern "C"` function returns anything by value; results are written into a buffer the caller owns and reuses.
 *   3. TEXT OUT has three shapes:
 *        (a) BOUNDED    `size_t f(…, char* out, size_t cap)` when a maximum is known and NAMED, so the caller declares `char buf[MAX]`.
 *                       Every function of this shape is internal (no consumer writes an escape sequence).
 *        (b) UNBOUNDED  `void f(…, RolltuiStr* out)`, REPLACING a buffer the caller owns and reuses.
 *        (c) BORROWED   `const char* f(…, size_t* len)`: memory the library keeps, its window stated on that function. Never yours to free.
 *   4. WORKING MEMORY is a handle the caller owns (`RolltuiDrawScratch`, …): one per thread, reused, freed. A callee invents none.
 *      4b. MANY THINGS OUT are a list the caller owns and reuses, REPLACED on every call (`RolltuiStrList`, `RolltuiRows`), never
 *      handed back through a callback.
 *   5. CALLBACKS cross as {function pointer, `void* ctx`, `void (*free_ctx)(void*)`} and carry only a decision into the library.
 *
 * C++ MEMBERS. Structs carry C++ members under `__cplusplus` for the language's sake only:
 *   - none names a `std::` container or view (`public_header_test` section 7); convert to `std::string` in your own file.
 *     `rolltui/str.hpp` has the view, `+`, `appendf` and list a host uses instead.
 *   - copy is deleted on every owning struct: spell it `x.clone()` or `a.assign(b)`, because `RolltuiLayer copy = *p;` would be a
 *     deep copy here and a shallow alias in C. Moves and destructors stay (RAII is the language's ownership model).
 *   - if two consumers write the same wrapper, the API is wrong: the fix goes into rolltui's own vocabulary, in C.
 *
 * The studio is rolltui's own authoring tool, not a consumer; the examples are consumers (`api_classes.inc` says why).
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
 * BOOK 1 — CORE: the primitives everything else is built from
 * Text, geometry, colour and style, the cell grid and drawing into it, JSON, Unicode, wrapping. Nothing here depends on anything
 * outside this book.
 * ======================================================================================== */

/* ========================================================================================
 * abi — the ABI macros every declaration below uses, and the code point
 * ======================================================================================== */

/* Both languages' `inline` and null pointer, spelled once. */
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
  // C++ members name rolltui's own types and the C standard's only (see `rolltui/str.hpp` for the view, `+` and list a host uses).
  // COPY IS DELETED: `RolltuiStr a = b;` would be a deep copy here and a shallow alias in C, so spell it `a.assign(b)`.
  // Move and the destructor stay; every other member is one C function with `this`.
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

/* Appends `text`, growing exactly: a name is built from a few pieces, not a stream. */
void rolltui_str_append(RolltuiStr* s, const char* text, size_t len);

/* Empties without releasing: the buffer is kept for the next fill. */
void rolltui_str_clear(RolltuiStr* s);

/* Releases the buffer and zeroes the struct. Safe on a zeroed struct and on NULL. */
void rolltui_str_free(RolltuiStr* s);

/* Takes `from`'s buffer and releases `to`'s old one; `from` is left empty. */
void rolltui_str_move(RolltuiStr* to, RolltuiStr* from);

int rolltui_str_eq(const RolltuiStr* s, const char* text, size_t len);

typedef struct RolltuiPtrVec {
  void** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);
} RolltuiPtrVec;

/* ---- the string SINK, for a function whose result is N strings ---------------------------
 * One `put` per string; `s` is a BORROW valid for the call only. */
typedef void (*RolltuiPutFn)(void* ctx, const char* s, size_t len);

/* MANY STRINGS OUT, into a list the caller owns and reuses (rule 3's shape for a list). Zero-initialise; `_release` frees it
 * (the C++ destructor does). `_add` appends a copy and returns a BORROW of the stored entry, valid until the next `_add`.
 * RolltuiStrListRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiStrList : public RolltuiStrListRaw`
 * reclaiming the name, with the destructor and accessors. */
typedef struct RolltuiStrListRaw {
  RolltuiStr* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);
} RolltuiStrListRaw;

void rolltui_str_list_release(RolltuiStrListRaw* l);

/* Bridge from the sink shape to a `RolltuiStr`: pass this as `put` and the `RolltuiStr*` as `ctx`. APPENDS, so clear the target
 * first when that is wanted. `RolltuiPutFn` is internal plumbing (and the type of a domain's hooks); no public function returns a
 * result through it (`public_header_test`). */
void rolltui_str_put(void* ctx, const char* s, size_t len);

#ifdef __cplusplus
namespace rolltui {
// `Str` is a value with one owner. `PtrVec` is not RAII-wrapped on purpose: each user owns elements of a different type, and
// freeing them must stay visible at the owner.
using Str = RolltuiStr;
using PtrVec = RolltuiPtrVec;

}  // namespace rolltui
#endif

/* ---- forward declarations the C++ members just below call ----------------------------------*/
void rolltui_str_set(RolltuiStr* s, const char* text, size_t len);

#ifdef __cplusplus
inline RolltuiStr::~RolltuiStr() { rolltui_str_free(this); }
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

/* ---- the key vocabulary, in BOTH its spellings ---------------------------------------------
 * One list, three columns: the C constant's suffix, the TitleCase display name (what `to_string(Event)` prints), and the lowercase
 * file name (what a bindings file spells; "" for `Char` and `Unknown`, which have no file word). Adding a key is one edit here.
 * ORDER IS ABI, same as `ROLLTUI_ROLE_LIST`: the ordinal crosses in every `RolltuiChord`. */
/* The lowercase words are what a BINDINGS FILE spells ("ctrl+k", "alt+left", "f2"); a chord naming none of them is a bad value with a
 * reason, never a silent miss. Shipped files: `rolltui/presets/bindings/`. F13 to F24 come after Unknown, so every key before them
 * keeps its number; only the kitty keyboard protocol tells them apart from Shift and F1 to F12. */
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
  X(UNKNOWN,   "Unknown",   "") \
  X(F13,       "F13",       "f13") \
  X(F14,       "F14",       "f14") \
  X(F15,       "F15",       "f15") \
  X(F16,       "F16",       "f16") \
  X(F17,       "F17",       "f17") \
  X(F18,       "F18",       "f18") \
  X(F19,       "F19",       "f19") \
  X(F20,       "F20",       "f20") \
  X(F21,       "F21",       "f21") \
  X(F22,       "F22",       "f22") \
  X(F23,       "F23",       "f23") \
  X(F24,       "F24",       "f24")

/* Aliases a bindings file also accepts and `chord_to_string` never prints. "space" names a CHAR chord (U+0020), so it carries its
 * codepoint rather than only an ordinal. */
#define ROLLTUI_KEY_ALIAS_LIST(X) \
  X("esc",   ESCAPE,   0) \
  X("pgup",  PAGEUP,   0) \
  X("pgdn",  PAGEDOWN, 0) \
  X("del",   DELETE,   0) \
  X("space", CHAR,     ' ')

typedef enum RolltuiKey {
#define ROLLTUI_KEY_ENUM_(UPPER, Title, file) ROLLTUI_KEY_##UPPER,
  ROLLTUI_KEY_LIST(ROLLTUI_KEY_ENUM_)
#undef ROLLTUI_KEY_ENUM_
  ROLLTUI_KEY_COUNT
} RolltuiKey;

/* One chord: a key, the character it is when the key is CHAR, and the modifiers (never the raw bytes). Super (the Windows or Command
 * key, `super+` in a bindings file) only the kitty keyboard protocol reports. */
typedef struct RolltuiChord {
  unsigned char key ROLLTUI_DEFAULT(ROLLTUI_KEY_CHAR);
  RolltuiCodepoint ch ROLLTUI_DEFAULT(0);
  unsigned char ctrl ROLLTUI_DEFAULT(0), alt ROLLTUI_DEFAULT(0), shift ROLLTUI_DEFAULT(0);
  unsigned char super ROLLTUI_DEFAULT(0);
  /* THE KEY ON A US LAYOUT, where the kitty keyboard protocol reports it and it differs from `ch` (Ctrl and the key that is C on a US
   * keyboard, on a Cyrillic one): a binding no chord matches is matched again on this, so `ctrl+c` is Ctrl-C on every layout. 0 when
   * not reported. Never in a bindings file. */
  RolltuiCodepoint base ROLLTUI_DEFAULT(0);
} RolltuiChord;

/* ---- the mouse ------------------------------------------------------------------------- */
/* `rolltui::MouseEvent` is this struct. `Kind` is spelled per language (C++ scoped enum, C byte) with a fixed underlying type, so
 * the two are one byte by the standard rather than by convention. */
typedef struct RolltuiMouseEvent {
#ifdef __cplusplus
  /* DoubleClick is the TERMINAL's: two presses of the same button on the same cell within `ROLLTUI_DOUBLE_CLICK_MS`. Both presses
   * are delivered; the DoubleClick follows the second. */
  enum class Kind : unsigned char { Press = 0, Release, Drag, Move, WheelUp, WheelDown, WheelLeft, WheelRight, DoubleClick };
  Kind kind = Kind::Press;
#else
  unsigned char kind; /* 0 press, 1 release, 2 drag, 3 move, 4-7 wheel up/down/left/right, 8 double-click */
#endif
  int x ROLLTUI_DEFAULT(0), y ROLLTUI_DEFAULT(0); /* 0-based cells */
  int button ROLLTUI_DEFAULT(0); /* 1 left, 2 middle, 3 right, 8 back, 9 forward (10, 11 the buttons past them); 0 for motion/wheel */
  unsigned char ctrl ROLLTUI_DEFAULT(0), alt ROLLTUI_DEFAULT(0), shift ROLLTUI_DEFAULT(0);
} RolltuiMouseEvent;

#define ROLLTUI_EVENT_KEY 0

#define ROLLTUI_EVENT_MOUSE 1

#define ROLLTUI_EVENT_PASTE 2

/* The terminal's window gained the person's focus (`focused` 1) or lost it (0): the terminal reports it (mode 1004), which every
 * terminal entered is asked to. `rolltui_run` hands it to the app's `focus` rather than to `event`. */
#define ROLLTUI_EVENT_FOCUS 5

/* ONE event. `text` is a BORROW valid only for the `emit` call: an Unknown key's raw bytes or a paste's contents; NULL otherwise.
 * A resize has no kind here: the Terminal makes it. */
typedef struct RolltuiEvent {
  unsigned char kind;
  RolltuiChord key;
  RolltuiMouseEvent mouse;
  const char* text;
  size_t text_len;
  unsigned char focused ROLLTUI_DEFAULT(0); /* ROLLTUI_EVENT_FOCUS: 1 gained, 0 lost */
} RolltuiEvent;

/* The three protocol names. ORDER IS ABI (`rolltui_key_active_protocol` returns the byte). */
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

/* The protocol the terminal turned out to speak: process-wide, Legacy until told otherwise. Retains nothing. */
unsigned char rolltui_key_active_protocol(void);
/* Whether this terminal, on its negotiated protocol, can deliver the chord at all (Ctrl-. and Ctrl-Enter need kitty or
 * modifyOtherKeys). Show the first chord that can arrive, not the first in the file. */
int rolltui_key_deliverable(const RolltuiChord* k, unsigned char protocol);

/* The bytes a terminal speaking `p` sends for the chord, into `out` (capacity `cap` >= ROLLTUI_KEY_ENCODE_MAX). Returns the length,
 * or -1 when `p` has no encoding. This is the encoding, not the verdict: the bytes may still be ambiguous. */
#define ROLLTUI_KEY_ENCODE_MAX 16

/* ========================================================================================
 * geom — RolltuiRect, the vocabulary every layout speaks
 * ======================================================================================== */

/* The intersection of two rectangles, into `out` as {x, y, w, h}. An empty result is {x0, y0, 0, 0} with (x0, y0) the clamped
 * origin, not {0,0,0,0}: callers position things relative to it. */
void rolltui_rect_intersect(int ax, int ay, int aw, int ah,
                            int bx, int by, int bw, int bh,
                            int out[4]);

/* ---- a rectangle, defined ONCE and compiled by both languages ------------------------- */
/* `rolltui::Rect` IS this struct. A layout node's outer and inner boxes are the split's whole output; the four-int forms below
 * remain because the C++ `intersect` is built on them. */
typedef struct RolltuiRect {
  int x ROLLTUI_DEFAULT(0), y ROLLTUI_DEFAULT(0), w ROLLTUI_DEFAULT(0), h ROLLTUI_DEFAULT(0);
} RolltuiRect;

ROLLTUI_STATIC_ASSERT(sizeof(RolltuiRect) == 16, "a Rect must be four ints in both languages");

/* ========================================================================================
 * style — RolltuiStyle, the role list, and colour depth/mode — how a value is spelled and shown
 * ======================================================================================== */

/* Every name here is a key a THEME FILE may set under "colours"/"roles"; a role a theme omits is reported, not silently defaulted.
 * Adding a role changes what every theme author may write. Shipped files: `rolltui/presets/themes/`. */
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
  X(hover, HOVER) \

typedef enum RolltuiRole {
#define ROLLTUI_ROLE_ENUM_(lower, UPPER) ROLLTUI_ROLE_##UPPER,
  ROLLTUI_ROLE_LIST(ROLLTUI_ROLE_ENUM_)
#undef ROLLTUI_ROLE_ENUM_
  ROLLTUI_ROLE_COUNT
} RolltuiRole;

/* The three that cross as struct-field DEFAULTS keep their names (a default has no call at which to hand a role in); they alias the enum. */
#define ROLLTUI_ROLE_DEFAULT_TEXT ROLLTUI_ROLE_TEXT

#define ROLLTUI_ROLE_DEFAULT_BACKGROUND ROLLTUI_ROLE_BACKGROUND

#define ROLLTUI_ROLE_DEFAULT_PROMPT ROLLTUI_ROLE_PROMPT

/* RolltuiStyleColorRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiStyleColor : public
 * RolltuiStyleColorRaw` with the static none()/indexed()/rgb() builders reclaiming the name every
 * existing C++ call site already uses; `operator==` is a free function there, not a member, so it
 * still applies through a RolltuiStyleColorRaw-typed field (RolltuiStyle::fg/bg and the rest). */
typedef struct RolltuiStyleColorRaw {
#ifdef __cplusplus
  enum class Kind : unsigned char { None = 0, Indexed = 1, Rgb = 2 };
  Kind kind = Kind::None;
#else
  unsigned char kind; /* 0 none, 1 indexed, 2 rgb */
#endif
  unsigned char index ROLLTUI_DEFAULT(0);              /* Indexed: 0-255 */
  unsigned char r ROLLTUI_DEFAULT(0), g ROLLTUI_DEFAULT(0), b ROLLTUI_DEFAULT(0); /* Rgb */
} RolltuiStyleColorRaw;

ROLLTUI_STATIC_ASSERT(sizeof(RolltuiStyleColorRaw) == 5, "RolltuiStyleColorRaw must be five bytes in both languages");

/* AN UNDERLINE'S SHAPE, in `RolltuiStyle.underline`. A terminal that draws only one sort of underline draws each of these as it. */
#define ROLLTUI_UNDERLINE_SINGLE 1
#define ROLLTUI_UNDERLINE_DOUBLE 2
#define ROLLTUI_UNDERLINE_CURLY 3 /* a spelling mistake, an error under a word */
#define ROLLTUI_UNDERLINE_DOTTED 4
#define ROLLTUI_UNDERLINE_DASHED 5

typedef struct RolltuiStyle {
  RolltuiStyleColorRaw fg ROLLTUI_DEFAULT({}), bg ROLLTUI_DEFAULT({});
  /* the underline's own colour (a red curl under a word in its text colour); none: the text's */
  RolltuiStyleColorRaw underline_color ROLLTUI_DEFAULT({});
  unsigned char bold ROLLTUI_DEFAULT(0), italic ROLLTUI_DEFAULT(0);
  unsigned char underline ROLLTUI_DEFAULT(0); /* 0 none, else ROLLTUI_UNDERLINE_*: 1 is a plain underline */
  unsigned char dim ROLLTUI_DEFAULT(0), reverse ROLLTUI_DEFAULT(0);
  unsigned char strike ROLLTUI_DEFAULT(0);   /* a line through the text: what was removed, what is done */
  unsigned char blink ROLLTUI_DEFAULT(0);    /* 1 slow, 2 rapid; a terminal that does not blink draws it steady */
  unsigned char hidden ROLLTUI_DEFAULT(0);   /* drawn in the background's colour: there, selectable, not shown */
  unsigned char overline ROLLTUI_DEFAULT(0); /* a line over the text */
} RolltuiStyle;

ROLLTUI_STATIC_ASSERT(sizeof(RolltuiStyle) == 24, "RolltuiStyle must be twenty-four bytes with no padding in either language");

/* ---- THE MODE AND DEPTH VOCABULARY ---------------------------------------------------------
 * Same move as `ROLLTUI_ROLE_LIST` and `ROLLTUI_EFFECT_STATE_LIST`: a vocabulary the C refuses to carry relocates into every caller
 * that cannot reach it, so the names live here once. ORDER IS ABI: the ordinal is what `rolltui_sgr`, `rolltui_color_downgrade` and
 * every renderer are handed. Mono stays 0 and TrueColor last: `rolltui_color_downgrade` compares against the constants, not a count. */
/* FOR THE FOURTH READER: the words a THEME FILE's "depth" may take, and `roll config set
 * color_depth` with them. Shipped files: `rolltui/presets/themes/`. */
#define ROLLTUI_DEPTH_LIST(X) \
  X("mono", MONO, Mono) \
  X("16", ANSI16, Ansi16) \
  X("256", ANSI256, Ansi256) \
  X("truecolor", TRUECOLOR, TrueColor)

/* The one ALIAS, and it belongs to the ENVIRONMENT, not the file format: COLORTERM and ROLL_COLOR_DEPTH accept "24bit", a preset
 * file's "depth" does not, and `color_depth_name` answers only "truecolor". A preset that stored "24bit" would round-trip to "truecolor"
 * and report a change nobody made. Only `rolltui_detect_color_depth` reads this list. */
#define ROLLTUI_DEPTH_ENV_ALIAS_LIST(X) X("24bit", TRUECOLOR)

typedef enum RolltuiColorDepth {
#define ROLLTUI_DEPTH_ENUM_(lower, UPPER, Camel) ROLLTUI_DEPTH_##UPPER,
  ROLLTUI_DEPTH_LIST(ROLLTUI_DEPTH_ENUM_)
#undef ROLLTUI_DEPTH_ENUM_
  ROLLTUI_DEPTH_COUNT
} RolltuiColorDepth;

/* FOR THE FOURTH READER: the words a THEME FILE's "mode" may take. Shipped files:
 * `rolltui/presets/themes/`. */
#define ROLLTUI_MODE_LIST(X) \
  X("dark", DARK, Dark) \
  X("light", LIGHT, Light)

typedef enum RolltuiThemeMode {
#define ROLLTUI_MODE_ENUM_(lower, UPPER, Camel) ROLLTUI_MODE_##UPPER,
  ROLLTUI_MODE_LIST(ROLLTUI_MODE_ENUM_)
#undef ROLLTUI_MODE_ENUM_
  ROLLTUI_MODE_COUNT
} RolltuiThemeMode;

/* The depth/mode of that name, or -1 when there is none. Neither accepts "auto" (that is the
 * SETTING layer below, not a depth) and neither accepts the env alias above. */
int rolltui_color_depth_from_name(const char* name, size_t len);

int rolltui_theme_mode_from_name(const char* name, size_t len);

/* The colour in the same spelling. "#rrggbb" is the longest, so seven bytes plus nothing —
 * the result is NOT terminated and the length is returned. */
#define ROLLTUI_COLOR_STRING_MAX 8

/* A COLOUR, BOTH WAYS. A typed `color` input field (`"kind": "input", "type": "color"`) hands the host back TEXT, so the round trip
 * is public. `parse`: "#rrggbb" | "none" | "0".."255"; 1 on success, 0 when it is not a colour. `to_string`: the same spelling back
 * into `cap` bytes (`ROLLTUI_COLOR_STRING_MAX` is enough); NOT terminated, the length is returned.
 * What is constrained: an EFFECT may never invent a colour (it picks the base style or a ROLE the theme named), which keeps `mono`
 * legible; a WIDGET drawing may write any colour into any cell (`rolltui_frame_put_text` and `_fill` take a style by value), and the
 * renderer down-converts at the frame's depth. */
int rolltui_color_parse(const char* text, size_t len, RolltuiStyleColorRaw* out);
/* COLOURS TOLD APART on a ground: `n` of them into `out`, each a hue of its own and each reading on `ground` at `min_contrast` (WCAG;
 * below 1: 4.5): a colour an author, a host or a series. `ground` that is not RGB is taken as black. Returns `n`. */
size_t rolltui_colors_distinct(RolltuiStyleColorRaw ground, size_t n, double min_contrast, RolltuiStyleColorRaw* out);
/* A style faded TOWARD a ground colour: `keep` 1 is the style itself, 0 is the ground. Only RGB
 * colours fade; a palette colour stays as it is. A fading status note, a column clipped at an edge. */
void rolltui_style_fade(const RolltuiStyle* st, RolltuiStyleColorRaw ground, double keep, RolltuiStyle* out);
/* A STYLE PATCHED OVER ANOTHER: `over`'s colours where it has them, its underline's shape and colour where it has one, its other
 * attributes added to `base`'s. A highlight over a row's own style, a row dimmed. */
void rolltui_style_patch(RolltuiStyle* base, const RolltuiStyle* over);

/* A style ON a ground: `fg`/`bg` "none" (kind 0) become `ground`; a stated colour is left as the theme wrote it. That is what "none"
 * in a theme file MEANS for a role that sits on whatever it is drawn over. Mutates in place; `ground` is typically the caller's
 * already-resolved background. */
void rolltui_style_on(RolltuiStyle* st, RolltuiStyleColorRaw ground);

size_t rolltui_color_to_string(RolltuiStyleColorRaw c, char* out, size_t cap);

/* The SGR sequence that selects `style` at `depth`, into `out`. Always starts from a reset, so a cell's style never depends on the
 * previous cell's. The longest is 77 bytes; the cap is a constraint on this file, not on a caller's data. */
#define ROLLTUI_SGR_MAX 96

/* ========================================================================================
 * screen and frame_ops — the cell grid, and drawing into it
 * ======================================================================================== */

/* One cell: a grapheme cluster, its style, its width and its hyperlink id. A cluster has no maximum length: ten bytes cover ASCII,
 * accented Latin, CJK, an emoji with a variation selector or skin tone, and a flag; anything longer (a ZWJ family is 25+ bytes)
 * SPILLS into a table the frame owns, its index kept where the bytes would be (`len` reads ROLLTUI_CELL_SPILLED), the same shape
 * `link` uses. */
#define ROLLTUI_CELL_INLINE_GLYPH 9

#define ROLLTUI_CELL_SPILLED 0xFF

/* RolltuiCellRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiCell : public
 * RolltuiCellRaw` reclaiming the name, with `spilled()` and the two constexpr constants. */
typedef struct RolltuiCellRaw {
  unsigned int link ROLLTUI_DEFAULT(0); /* 0: none; else an id from `rolltui_frame_link_id` */
  RolltuiStyle style;
  char bytes[ROLLTUI_CELL_INLINE_GLYPH] ROLLTUI_DEFAULT({' '}); /* the cluster, or its spill index */
  unsigned char len ROLLTUI_DEFAULT(1);   /* bytes in `bytes`; 0 on a continuation cell */
  unsigned char width ROLLTUI_DEFAULT(1); /* 1 or 2; 0 on a continuation cell */
  unsigned char continuation ROLLTUI_DEFAULT(0); /* the right half of a 2-cell glyph */
} RolltuiCellRaw;

/* THE CELL HAS NO PADDING, asserted: `rolltui_frame_equal` compares cells with `memcmp`, which is only right when every byte was written. */
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiCellRaw) == 4 + 24 + ROLLTUI_CELL_INLINE_GLYPH + 3,
                      "RolltuiCellRaw has padding; memcmp equality would compare bytes nobody wrote");

typedef struct RolltuiFrame RolltuiFrame;

/* Reuses every buffer it can: the cells, the link table's strings and the
 * spill table's. A steady frame allocates nothing through here. */

typedef struct RolltuiDrawScratch RolltuiDrawScratch;

/* `state` is rolltui::EffectState as an int; the C side stores it and never interprets it, which keeps the effects vocabulary in one
 * place. 0 means None and is not recorded, so "is anything marked" and "does anything move" stay the same question. */
void rolltui_frame_mark(RolltuiFrame* f, int x, int y, int cells, int state,
                        unsigned long long since_ms, double fraction);

size_t rolltui_frame_mark_count(const RolltuiFrame* f);

/* ---- cursor ------------------------------------------------------------------------ */
/* Where the terminal's cursor is left after the frame, and whether it shows. A focused list, menu or column browser leaves it, hidden,
 * on its cursor's row, where a screen reader looks. */
void rolltui_frame_set_cursor(RolltuiFrame* f, int x, int y, int visible);
/* THE CURSOR'S SHAPE while it shows (DECSCUSR): ROLLTUI_CARET_*; 0 the terminal's own. Sent when it changes, put back when the
 * program leaves; never sent to the Linux console. */
#define ROLLTUI_CARET_DEFAULT 0
#define ROLLTUI_CARET_BLOCK_BLINK 1
#define ROLLTUI_CARET_BLOCK 2
#define ROLLTUI_CARET_UNDERLINE_BLINK 3
#define ROLLTUI_CARET_UNDERLINE 4
#define ROLLTUI_CARET_BAR_BLINK 5
#define ROLLTUI_CARET_BAR 6
void rolltui_frame_set_cursor_shape(RolltuiFrame* f, unsigned char shape);

RolltuiDrawScratch* rolltui_draw_scratch_new(void);

void rolltui_draw_scratch_free(RolltuiDrawScratch* s); /* a no-op on NULL */

/* Writes `utf8` at (x, y), cluster by cluster, stopping at `max_cells`, at the frame's right
 * edge, or before a wide glyph that would be cut in half. Returns the cells used. */
int rolltui_frame_put_text(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                           RolltuiStyle style, int max_cells, int ambiguous_wide, unsigned int link);
/* A HYPERLINK: the id `rolltui_frame_put_text` takes as `link`, for `url` in this frame. Text drawn with it is sent as an OSC 8
 * hyperlink, which a terminal that has them opens on a click. The same URL gets the same id; an empty one gets 0, no link. Ids
 * belong to the frame and a new frame starts with none, so a host asks for its links each time it draws. */
unsigned int rolltui_frame_link_id(RolltuiFrame* f, const char* url, size_t url_len);

/* ---- a colour, shown ---------------------------------------------------------------------
 * A COLOUR VALUE'S SWATCH, wherever one is drawn (a theme editor's role, a menu's colour input, a painter's ink): two cells showing
 * the colour as it will be drawn (through the same renderer, at the terminal's depth), framed in the text's own foreground so it
 * shows against any ground. A `none` colour is the frame alone. A caller wanting the spelling beside it (`rolltui_color_to_string`'s
 * "#rrggbb", palette index or "none") draws that itself, as every real caller already does. */

/* The swatch: `frame`'s foreground is the outline and its background is where a `none` colour lands. Returns the cells used: 2, or 0
 * where they do not fit (`max_cells` < 2) or the terminal draws no colour. Where an ambiguous glyph is two cells the outline is a bracket
 * pair, one cell wherever it is drawn. */
int rolltui_frame_put_swatch(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, RolltuiStyleColorRaw colour,
                             RolltuiStyle frame, int max_cells, int ambiguous_wide);

/* Fills `r` (clipped) with a repeated grapheme — a space when `glyph` is NULL or has no
 * width. */
void rolltui_frame_fill(RolltuiFrame* f, RolltuiDrawScratch* s, RolltuiRect r, RolltuiStyle style,
                        const char* glyph, size_t glyph_len);

/* ONE ROW OF NAME/VALUE FIELDS, each name in `name_style` and each value in `value_style`, separated by two spaces and stopping at
 * `max_cells`. Returns the cells used. It is what a status line is: a handful of facts, each with a name (one string in one style is a
 * wall of words). A row with an EMPTY LABEL draws its value alone (a title, a bracketed note); an empty VALUE draws the name alone (a
 * flag). It takes a `RolltuiRows` the caller owns and refills, so a warm frame allocates nothing: `rolltui_rows_reset` keeps the array
 * and every row's buffer. RolltuiRows itself belongs to the widgets book; only a pointer is needed here, so a forward declaration
 * suffices rather than pulling a widget-registry type into Core. */
typedef struct RolltuiRows RolltuiRows;

int rolltui_frame_put_fields(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const RolltuiRows* rows,
                             RolltuiStyle name_style, RolltuiStyle value_style, int max_cells,
                             int ambiguous_wide);

/* TEXT CUT TO FIT, with an ellipsis where it was cut: at its END ("a long na…"), in its MIDDLE ("~/src/…/main.c", a path keeping
 * both ends) or at its START ("…/main.c"). Text that fits is drawn whole. The ellipsis is one cell, or two where ambiguous glyphs are
 * wide, and a wide glyph is never cut in half. Returns the cells used. */
#define ROLLTUI_CUT_END 0
#define ROLLTUI_CUT_MIDDLE 1
#define ROLLTUI_CUT_START 2
int rolltui_frame_put_text_cut(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len, RolltuiStyle style,
                               int max_cells, int where, int ambiguous_wide, unsigned int link);
/* The same cut, as text: REPLACES `*out` with `utf8` cut to `max_cells`; returns the cells it takes. */
int rolltui_text_cut(RolltuiDrawScratch* s, const char* utf8, size_t len, int max_cells, int where, int ambiguous_wide, RolltuiStr* out);

/* ---- the log ------------------------------------------------------------------------------------
 * THE TERMINAL IS THE SCREEN'S while a program runs, so what the library and a host say for whoever is looking goes to a FILE: the one
 * `rolltui_log_to` names (a host's, say in its state directory), else the one the environment's `ROLLTUI_LOG` names; with neither,
 * nothing is written. A line is the local time, the seconds since the log began, the level and the text, one line a call; lines from
 * workers never land inside one another. `ROLLTUI_LOG_LEVEL` (error, warn, info, debug) or `rolltui_log_set_level` says what is let
 * through; INFO by default. The library says what terminal it entered and how it draws, a store read again because another program
 * changed it, and, at DEBUG, each frame's time. */
#define ROLLTUI_LOG_ERROR 0
#define ROLLTUI_LOG_WARN 1
#define ROLLTUI_LOG_INFO 2
#define ROLLTUI_LOG_DEBUG 3
/* Appended to; NULL or "" goes back to `ROLLTUI_LOG`. 1 when the file is open. */
int rolltui_log_to(const char* path, size_t len);
void rolltui_log_set_level(int level);
/* 1 when a line of `level` would be written: what a caller asks before building one that costs. */
int rolltui_log_wants(int level);
void rolltui_log(int level, const char* text, size_t len);
void rolltui_logf(int level, const char* fmt, ...)
#if defined(__MINGW32__) /* the UCRT's printf, the C99 one with %zu, which MinGW names gnu_printf; plain `printf` is Microsoft's old one */
    __attribute__((format(gnu_printf, 2, 3)))
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* ---- numbers for a person ---------------------------------------------------------------------
 * A SIZE, as a column of them reads: steps of 1024 (K, M, G, T, P, E) or, with DECIMAL, of 1000 (k, M, G, T, P, E), one decimal under
 * 10 and none from 10 ("1.5K", "12M", "512"), the letter straight after; a value that rounds up to the next step is written in it
 * ("1.0M", never "1024K"). BITS writes a count of bytes as bits, eight a byte, with `b` after ("12Mb"); RATE adds "/s"; BYTE_UNIT
 * writes a size under the first step with its unit ("512 B", "512 b"). Into `out` (`cap` bytes, always terminated); returns the bytes
 * written. */
#define ROLLTUI_SIZE_DECIMAL 1u
#define ROLLTUI_SIZE_BITS 2u
#define ROLLTUI_SIZE_RATE 4u
#define ROLLTUI_SIZE_BYTE_UNIT 8u
size_t rolltui_format_size(double n, unsigned flags, char* out, size_t cap);
/* A COUNT WITH ITS THOUSANDS GROUPED by `sep` ("12,345,678"; NULL or "": a comma, the one an English reader expects; a thin space
 * U+2009 or a period for another). Into `out` (`cap` bytes, always terminated); returns the bytes written. */
size_t rolltui_format_count(long long n, const char* sep, size_t sep_len, char* out, size_t cap);
/* Every cell of `r` patched with `style` (`rolltui_style_patch`): a row dimmed, a match lit. */
void rolltui_frame_tint(RolltuiFrame* f, RolltuiRect r, RolltuiStyle style);

/* THE CURSOR ROW'S SHAPE. FILL: the row in the cursor's style. CAPS: the same, its two ends rounded by a half block each (▐…▌).
 * EDGES: the row on its own ground, its text in the cursor's colour, a bar at each end (▌…▐), so a terminal's own background
 * shows through. Where ambiguous glyphs are wide, CAPS and EDGES draw as FILL. */
#define ROLLTUI_CURSOR_FILL 0
#define ROLLTUI_CURSOR_CAPS 1
#define ROLLTUI_CURSOR_EDGES 2
/* The style a cursor row's text takes in `shape`, into `*out`: the cursor's own, or for EDGES its colour on `ground`. */
void rolltui_cursor_text_style(const RolltuiStyle* cursor, RolltuiStyleColorRaw ground, unsigned char shape, RolltuiStyle* out);
/* The ends of a cursor row: the first and last cells of `band`, which the widget left blank, drawn for `shape`. */
void rolltui_frame_cursor_ends(RolltuiFrame* f, RolltuiRect band, RolltuiStyle cursor, RolltuiStyleColorRaw ground, unsigned char shape);

/* A BOX a widget draws inside its own content, with no joining: a group, a nested section. The kinds a layout file names
 * ("border"): lines thin, rounded, double and heavy; blocks a half cell inside or outside the edge; hidden, a ring of the box's
 * ground that keeps its room. Where ambiguous glyphs are wide, + - |. `title` may be NULL when `title_n` is 0. */
#define ROLLTUI_BORDER_NONE 0
#define ROLLTUI_BORDER_SINGLE 1
#define ROLLTUI_BORDER_ROUNDED 2
#define ROLLTUI_BORDER_DOUBLE 3
#define ROLLTUI_BORDER_HEAVY 4
#define ROLLTUI_BORDER_HIDDEN 5
#define ROLLTUI_BORDER_QUADRANT_INSIDE 6
#define ROLLTUI_BORDER_QUADRANT_OUTSIDE 7
#define ROLLTUI_BORDER_COUNT 8
void rolltui_draw_border(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char border, RolltuiStyle line,
                         const char* title, size_t title_n, RolltuiStyle title_style, int ambiguous_wide);

/* A CUT WITH ITS OWN RULES. `marker` stands where text was cut (NULL: an ellipsis, as above; "~" as lf draws it). `keep_extension`
 * keeps a file name's extension whole after the cut ("a_long_rep….pdf"). `split` is the percent of the room a middle cut gives the
 * head (0: half). */
typedef struct RolltuiCutOptions {
  unsigned char where ROLLTUI_DEFAULT(ROLLTUI_CUT_END);
  unsigned char keep_extension ROLLTUI_DEFAULT(0);
  unsigned char split ROLLTUI_DEFAULT(0);
  const char* marker ROLLTUI_DEFAULT(nullptr); /* BORROWED for the call; NUL-terminated */
} RolltuiCutOptions;
int rolltui_text_cut_with(RolltuiDrawScratch* s, const char* utf8, size_t len, int max_cells, const RolltuiCutOptions* o,
                          int ambiguous_wide, RolltuiStr* out);
int rolltui_frame_put_text_cut_with(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                                    RolltuiStyle style, int max_cells, const RolltuiCutOptions* o, int ambiguous_wide);
/* A PATH SHORTENED TO FIT: each folder from the left down to its first letter (a leading dot kept: ".config" is ".c") until it fits,
 * then the name itself cut in its middle, its extension kept: "~/git/rolltui/c/frame.c" in 15 cells is "~/g/r/c/frame.c". REPLACES
 * `*out`; returns the cells it takes. */
int rolltui_path_shorten(RolltuiDrawScratch* s, const char* path, size_t len, int max_cells, int ambiguous_wide, RolltuiStr* out);

/* A LINE OF SPANS, each its own style, laid into `r`: at its left, centre or right, and where it is wider than `r`, cut at the edge,
 * cut with an ellipsis, or wrapped at word breaks onto the rows below. Returns the rows it used. */
typedef struct RolltuiTextSpan {
  const char* text; /* BORROWED for the call */
  size_t len;
  RolltuiStyle style;
} RolltuiTextSpan;
#define ROLLTUI_ALIGN_LEFT 0
#define ROLLTUI_ALIGN_CENTER 1
#define ROLLTUI_ALIGN_RIGHT 2
#define ROLLTUI_OVERFLOW_CLIP 0
#define ROLLTUI_OVERFLOW_ELLIPSIS 1
#define ROLLTUI_OVERFLOW_WRAP 2
int rolltui_frame_put_spans(RolltuiFrame* f, RolltuiDrawScratch* s, RolltuiRect r, const RolltuiTextSpan* spans, size_t n,
                            unsigned char align, unsigned char overflow, int ambiguous_wide);
/* THE LETTER A KEY NAMES in a label, to light it: the first word that starts with `ch` (case and accents aside), else the first
 * place `ch` is at all. Its byte offset, with its length in `*n`; (size_t)-1 when the label has none. "Save _as" for a, "E_xit"
 * for x. */
size_t rolltui_hotkey_at(const char* label, size_t len, unsigned int ch, size_t* n);

/* The cells `utf8` WOULD occupy, measured with the same cluster walk that draws it (`rolltui_frame_put_text` answers only after drawing,
 * and a caller sizing a column must know first). Same walk, same ambiguous-width rule, same result. */
int rolltui_frame_text_width(RolltuiDrawScratch* s, const char* utf8, size_t len, int ambiguous_wide);

/* A BORROW into the caller's own table, valid exactly as long as `styles` is. NULL when
 * `styles` is NULL or `role` is out of range — a bad ordinal is a caller bug, not a crash. */
const RolltuiStyle* rolltui_theme_style(const RolltuiStyle* styles, size_t role_count, unsigned char role);

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

/* One member of an object: a key and its OWNED value. `obj` keeps these in insertion order (a theme's role list round-trips in the
 * order the author wrote it). */
typedef struct RolltuiJsonMember {
  RolltuiStr key;
  RolltuiJsonValue* value ROLLTUI_DEFAULT(nullptr); /* OWNED; never NULL on a complete value */
} RolltuiJsonMember;

/* A plain struct with every member always present: `kind` says which of `str`/`arr`/`obj` is meaningful, but `_free`, `_clone` and
 * `_equal` never gate on it, so retagging a value (as `rolltui_json_set` does) cannot orphan a buffer. */
struct RolltuiJsonValue {
  unsigned char kind ROLLTUI_DEFAULT(ROLLTUI_JSON_NULL);
  unsigned char b ROLLTUI_DEFAULT(0);
  double num ROLLTUI_DEFAULT(0);
  RolltuiStr str;
  /* ARRAY children: an owned array of owned value pointers, laid out as `RolltuiPtrVec` with its amortised growth, so an element's
   * address never moves. */
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

RolltuiJsonValue* rolltui_json_string(const char* s, size_t len);

RolltuiJsonValue* rolltui_json_array(void);

void rolltui_json_free(RolltuiJsonValue* v); /* recursive; a no-op on NULL */

/* A deep copy the caller owns; NULL in, NULL out (mirrors `Value`'s copy constructor). */
RolltuiJsonValue* rolltui_json_clone(const RolltuiJsonValue* v);

/* Deep, order-sensitive structural equality: compares every member unconditionally rather than only the ones `kind` says are live. */

int rolltui_json_is_bool(const RolltuiJsonValue* v);

int rolltui_json_is_number(const RolltuiJsonValue* v);

/* Typed reads with defaults; never fail. A BORROW valid as long as `v` (or `def`) is. */
int rolltui_json_as_bool(const RolltuiJsonValue* v, int def);
const char* rolltui_json_as_string(const RolltuiJsonValue* v, const char* def, size_t def_len, size_t* out_len);

/* Object lookup: a BORROW, never NULL: a static Null (valid forever) when `v` is not an object or the key is absent, so lookups chain. */
const RolltuiJsonValue* rolltui_json_get(const RolltuiJsonValue* v, const char* key, size_t key_len);

/* Object insert-or-replace. ALWAYS turns `v` into an object (nothing is cleared, which is safe because free/clone/equal never gate on
 * `kind`). TAKES OWNERSHIP of `child`; a replaced value is freed. Returns a BORROW of the stored child. */
RolltuiJsonValue* rolltui_json_set(RolltuiJsonValue* v, const char* key, size_t key_len, RolltuiJsonValue* child);

/* Removes `key` if present, freeing the value; 1 when something was removed. Order-preserving. KEPT with `get`/`has`/`set`: an object
 * API that can add a key but not remove one forces a rebuild. */
int rolltui_json_object_erase(RolltuiJsonValue* v, const char* key, size_t key_len);

size_t rolltui_json_array_size(const RolltuiJsonValue* v);

RolltuiJsonValue* rolltui_json_array_at(const RolltuiJsonValue* v, size_t i); /* BORROW; NULL out of range */

/* Appends. TAKES OWNERSHIP of `child`. `v` must already be an array (`rolltui_json_array()`): there is no coercion. */
void rolltui_json_array_push(RolltuiJsonValue* v, RolltuiJsonValue* child);

/* Parses `text`. Returns an OWNED value the caller frees, or NULL on failure. `error` may be
 * NULL when the caller does not care; otherwise it is cleared on entry and set to
 * "line N: message" on the first failure only, and left empty on success. */
RolltuiJsonValue* rolltui_json_parse(const char* text, size_t len, RolltuiStr* error);

/* Serialises deterministically into `out`, REPLACING its contents. indent = 0 -> single
 * line. */
void rolltui_json_dump(const RolltuiJsonValue* v, int indent, RolltuiStr* out);

/* ========================================================================================
 * unicode — the library's Unicode algorithms
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
/* THE GROWING BUFFERS THESE ALGORITHMS NEED, owned by the caller and reused across calls: the functions marked below need somewhere to
 * decode into, mark boundaries in and build line-break units in. One handle per thread, made once; after the first few calls it never
 * grows, so the draw path allocates nothing. One buffer per ROLE, so a function that calls another (graphemes -> boundaries,
 * display_width -> graphemes) cannot alias its own scratch. */
typedef struct RolltuiUnicodeScratch RolltuiUnicodeScratch;

/* ---- sanitising ------------------------------------------------------------------------ */
/* Removes terminal control sequences from text that will be RENDERED. `out` must hold at least
 * `len` bytes — stripping only ever removes. Returns the bytes written. */
size_t rolltui_u_strip_escape_sequences(const char* s, size_t len, char* out);

RolltuiUnicodeScratch* rolltui_u_scratch_new(void);

void rolltui_u_scratch_free(RolltuiUnicodeScratch* s);

int rolltui_u_display_width(RolltuiUnicodeScratch* s, const char* utf8, size_t len, int ambiguous_wide);

/* How many BYTES of `utf8` fit in `max_cells` columns: the offset `rolltui_frame_put_text` computes to honour its own `max_cells`.
 * Returns that byte length and fills `*out_cells` (may be NULL) with the columns they occupy. Stops BEFORE a glyph that would be cut in
 * half, put_text's rule, so the two agree about where a cut falls. A widget that truncates needs it to place an ellipsis, and every
 * list, tree, table and column view truncates. `s` is the caller's scratch (rule 4). */
size_t rolltui_u_fit(RolltuiUnicodeScratch* s, const char* utf8, size_t len, int max_cells,
                     int ambiguous_wide, int* out_cells);

/* ---- case ------------------------------------------------------------------------------ */
/* WHAT `rolltui_u_case` DOES to each code point, by Unicode's simple mappings (one code point to one: `ß` stays `ß` uppercased, and
 * the text can change length in bytes). FOLD is for caseless matching: two texts that differ only in case fold to the same text. TITLE
 * titles each word's first cased letter and lowers the rest of the word. */
#define ROLLTUI_CASE_UPPER 0
#define ROLLTUI_CASE_LOWER 1
#define ROLLTUI_CASE_FOLD 2
#define ROLLTUI_CASE_TITLE 3
/* REPLACES `*out` with `utf8` mapped by `how`. Bytes that are not UTF-8 pass through unchanged. */
void rolltui_u_case(RolltuiUnicodeScratch* s, const char* utf8, size_t len, int how, RolltuiStr* out);

/* ========================================================================================
 * wrap — the wrap engine roll draws with
 * ======================================================================================== */

/* `ambiguous_wide` is `unsigned char`, not `bool`, for `rolltui_style.h`'s reason. The field order is the C++ struct's, because two
 * call sites write it as an order-sensitive designated initializer. */
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

/* Drops the LIVE LINES and keeps every buffer, so a handle wrapped into repeatedly allocates nothing after its first few calls (what
 * `Scratch` calls on acquire and release). */

/* ---- lifetime ------------------------------------------------------------------------ */
/* OWNED by the caller. `new` never returns NULL: an allocation failure aborts inside
 * rolltui::mem, because there is nothing useful to do with a half-built wrap. */
RolltuiWrapLines* rolltui_wrap_new(void);

void rolltui_wrap_free(RolltuiWrapLines* w);

/* Wraps `len` bytes of UTF-8 to `width` cells, into `w`, reusing everything `w` holds. `width <= 0` is legal and draws nothing (one
 * empty line per paragraph); empty input is one empty line. */
void rolltui_wrap(RolltuiWrapLines* w, const char* utf8, size_t len, int width, RolltuiWrapOptions opt);

/* ---- reading the lines ----------------------------------------------------------------- */
size_t rolltui_wrap_line_count(const RolltuiWrapLines* w);

/* Line `i` as BORROWS into `w` (rule 3(c)). `text` is NOT NUL-terminated: `text_len` is the length, and a zero-length line gives a valid
 * non-NULL pointer. `hard` receives 1 when the line was ended by a mandatory break or by the end of the text. */
void rolltui_wrap_line(const RolltuiWrapLines* w, size_t i, const char** text, size_t* text_len,
                       const RolltuiWrapGrapheme** graphemes, size_t* grapheme_count,
                       int* width, int* indent, int* hard);

/* ========================================================================================
 * mem — the leak gauge — public by stated reason: it is how a consumer proves it released everything
 * ======================================================================================== */

/* Resets the CUMULATIVE counters only (`allocations`, `frees`, `bytes_requested`). `live_bytes` and `live_blocks` describe storage that
 * still exists; `peak_bytes` is re-based to what is live now. For a test that wants a window; never called by the library. */
/* MEMORY USAGE, QUERYABLE AT RUNTIME: the allocator's counters as out-params, any of which may be NULL. The three byte numbers answer
 * different questions: `bytes_requested` is CUMULATIVE (a growing buffer is counted again at every growth: a churn signal, not how much
 * is held); `live_bytes` is HELD RIGHT NOW as the allocator's usable size (what a status pane means by "memory usage"); `peak_bytes` is
 * the high-water mark of `live_bytes` (for a library built on reusing buffers, how big the reuse ever had to get). */
void rolltui_mem_stats(size_t* allocations, size_t* frees, size_t* bytes_requested,
                       size_t* live_bytes, size_t* peak_bytes, size_t* live_blocks);

/* ---- the library's one entry point, and the only two halves of it a CONSUMER may name -----
 * `rolltui_mem_alloc` and `rolltui_mem_free`, declared here so a consumer that wants a handle and its release can reach them.
 * `rolltui_mem_realloc` is deliberately internal: growth is what the closed set exists to stop being invented. An allocation failure
 * ABORTS rather than returning NULL, so neither can fail and no caller checks. Every allocation in the library goes through these,
 * which is why `rolltui_mem_stats` above can be believed. */
void* rolltui_mem_alloc(size_t bytes);

void rolltui_mem_free(void* p);

/* ========================================================================================
 * opaque handles — declared once, here, so every later book may reference one freely without an ordering dependency
 * Never used by value anywhere in this header (rule 2 forbids returning one from an `extern "C"` function, and none appear as a
 * by-value struct field either), so forward-declaring each once, early, costs nothing and removes any ordering constraint a later
 * book's module would otherwise have on wherever that type's own book happens to sit.
 * ======================================================================================== */

typedef struct RolltuiContext RolltuiContext;
typedef struct RolltuiWindows RolltuiWindows;
typedef struct RolltuiWindowStack RolltuiWindowStack;
typedef struct RolltuiTerminal RolltuiTerminal;
typedef struct RolltuiSwap RolltuiSwap;
typedef struct RolltuiAnsiText RolltuiAnsiText;
typedef struct RolltuiImage RolltuiImage;
typedef struct RolltuiTasks RolltuiTasks;
typedef struct RolltuiJob RolltuiJob;
typedef struct RolltuiBindings RolltuiBindings;
typedef struct RolltuiLayout RolltuiLayout;
typedef struct RolltuiLayoutNode RolltuiLayoutNode;
typedef struct RolltuiLayer RolltuiLayer;
typedef struct RolltuiLayerList RolltuiLayerList;
typedef struct RolltuiContent RolltuiContent;
typedef struct RolltuiActionList RolltuiActionList;
typedef struct RolltuiNodeList RolltuiNodeList;
typedef struct RolltuiMenu RolltuiMenu;
typedef struct RolltuiInput RolltuiInput;
typedef struct RolltuiTranscript RolltuiTranscript;
typedef struct RolltuiMdLines RolltuiMdLines;
typedef struct RolltuiEffectMap RolltuiEffectMap;
typedef struct RolltuiSettings RolltuiSettings;

/* ========================================================================================
 * BOOK 2 — CONFIGURATION: the four user/host-authored file types, and the stores that manage them
 * Theme, bindings, layout, menu — what a person or a host author writes to a file — plus the embedded shipped defaults and the
 * settings handle that routes a changed key to the right one.
 * ======================================================================================== */

/* Forward declares of the `presets` module's own types (below, this book): each of the three stores' `_get`/`_list` functions
 * takes one by pointer only, so a forward declare is enough here and the real, full definition stays with `presets`, beside the
 * rest of the generic preset machinery it belongs to. */
typedef struct RolltuiThemePresetReport RolltuiThemePresetReport;
typedef struct RolltuiLayoutPresetReport RolltuiLayoutPresetReport;
typedef struct RolltuiBindingsPresetReport RolltuiBindingsPresetReport;
typedef struct RolltuiPresetListRaw RolltuiPresetListRaw;
typedef struct RolltuiThemePresetValue RolltuiThemePresetValue;
/* Same reason, forward from `embedded`: `layout_tree`'s `rolltui_app_file` takes one by pointer before `embedded`'s own section
 * (later in this book) gives it its real, full definition. */
typedef struct RolltuiEmbeddedFile RolltuiEmbeddedFile;
/* A pure predicate over a "mode"/"depth" string, with no context to capture. A function-pointer typedef has no incomplete form, so
 * (unlike the structs above) this is `presets`' real, only definition, declared early because `theme`'s preset parser needs it. */
typedef int (*RolltuiThemePresetValidFn)(const char* s, size_t len);

/* ========================================================================================
 * bindings — a host holds a bindings table and its report
 * ======================================================================================== */

/* The canonical spelling and the help form; "" for an Unknown key. Thirty-two is past the longest either produces. */
#define ROLLTUI_CHORD_STRING_MAX 32

/* ---- the table --------------------------------------------------------------------------- */
/* OWNED, LONG-LIVED: one per `rolltui::Bindings`, which frees it. Three parallel lists: the DECLARED actions with their descriptions,
 * and the ROWS (action -> chords). They are separate because a row may outlive a declaration: a bindings file is global and the
 * user's, so a row for another screen's action is KEPT and inert.
 * Answers whether a scope is one the library defines — the caller's fact, asked for by
 * `rolltui_bindings_undeclare_others` below. */
typedef int (*RolltuiScopeFn)(void* ctx, const char* scope, size_t len);

/* The report, transparent like every other on this boundary: `RolltuiStr` values in growing arrays, one per load-report field.
 * Zero-initialise before use. */

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

/* The English for why a chord cannot be delivered, into a caller buffer of at least ROLLTUI_UNDELIVERABLE_REASON_MAX bytes.
 * Returns the length written. */

#define ROLLTUI_UNDELIVERABLE_REASON_MAX 128

typedef size_t (*RolltuiReasonFn)(void* ctx, const RolltuiChord* k, unsigned char protocol, char* out, size_t cap);

/* ---- DECLARING, SUGGESTING, AND THE SHIPPED TABLE -----------------------------------------
 * `RolltuiToolAction` is the row a MOUNTED TOOL brings: its name, its English and the chord it SUGGESTS. A tool states its keys in
 * code because no bindings file can: the shipped file belongs to every host, and a row in it for a tool most hosts never mount would
 * advertise a key they cannot press. */
/* A FORWARD declaration, never an include: `rolltui_layout.h` includes this header, so including it back would be a cycle.
 * `rolltui_bindings_declare` only takes a pointer. */
typedef struct RolltuiLayoutAction RolltuiLayoutAction;

typedef struct RolltuiToolAction {
  const char* name;
  const char* description;
  const char* chord; /* "ctrl+q" — a SUGGESTION, never an override. NULL or "" for none. */
} RolltuiToolAction;

/* ---- the bindings store: USE — same shape as the theme store, minus the person's settings ----
 * No `_set_*` either: a bindings table has nothing beside its chords that is the person's own to carry — the file itself already is
 * the person's. `_new` keeps `RolltuiContext*` for the same reason the theme store's does. */
typedef struct RolltuiBindingsStore RolltuiBindingsStore;

RolltuiBindingsStore* rolltui_bindings_store_new(RolltuiContext* c, const char* dir, size_t dir_len, int may_write_shipped,
                                                 const char* shipped_dir, size_t shipped_dir_len);
void rolltui_bindings_store_free(RolltuiBindingsStore* s);
void rolltui_bindings_store_start(RolltuiBindingsStore* s, RolltuiBindingsPresetReport* report);
/* THE FILES READ AGAIN when another program changed them (the studio, an editor): the working copy as it now is, or, while it has no
 * edits of its own, the preset it follows. 1 when it was read again, its version moved as an edit's would; `report` RESET says how
 * it went. This store's own writes never count. `rolltui_run_watch_stores` calls it when the files change. */
int rolltui_bindings_store_refresh(RolltuiBindingsStore* s, RolltuiBindingsPresetReport* report);
/* A BORROW of the working table, valid until the next call that changes it (`_load`, or — `rolltui_studio.h` — `_edit`), for a host
 * whose every change happens on the reading thread; across threads, `_copy_working`, as the theme store's. */
const RolltuiBindings* rolltui_bindings_store_working(const RolltuiBindingsStore* s);
/* An OWNED copy, taken under the store's lock, as the theme store's. Free it with `rolltui_bindings_free`. */
RolltuiBindings* rolltui_bindings_store_copy_working(const RolltuiBindingsStore* s);
int rolltui_bindings_store_modified(const RolltuiBindingsStore* s);
void rolltui_bindings_store_label(const RolltuiBindingsStore* s, RolltuiStr* out);
unsigned long long rolltui_bindings_store_version(const RolltuiBindingsStore* s);

/* A BORROW of the origin's name alone, same as the theme store's. */
const char* rolltui_bindings_store_origin(const RolltuiBindingsStore* s, size_t* len);
void rolltui_bindings_store_list(const RolltuiBindingsStore* s, RolltuiPresetListRaw* out);
/* OWNED: free the result with `rolltui_bindings_free`. */
RolltuiBindings* rolltui_bindings_store_get(const RolltuiBindingsStore* s, const char* name, size_t len,
                                            RolltuiBindingsPresetReport* report);
int rolltui_bindings_store_load(RolltuiBindingsStore* s, const char* name, size_t len,
                                RolltuiBindingsPresetReport* report, int persist);
/* Save the working table under a name, and add a file someone sent: the same contract as the theme store's. */
int rolltui_bindings_store_save_as(RolltuiBindingsStore* s, const char* name, size_t len, int overwrite,
                                   RolltuiStr* err);
int rolltui_bindings_store_add(RolltuiBindingsStore* s, const char* path, size_t path_len, const char* as,
                               size_t as_len, RolltuiStr* err);
void rolltui_bindings_store_path(const RolltuiBindingsStore* s, const char* name, size_t len, RolltuiStr* out);
int rolltui_bindings_is_shipped(RolltuiContext* c, const char* name, size_t len);
void rolltui_bindings_shipped_names(RolltuiContext* c, RolltuiStrListRaw* out);
const RolltuiBindings* rolltui_bindings_shipped(RolltuiContext* c, const char* name, size_t len);

/* ---- the table: parse, query, bind, declare -------------------------------------------------- */

/* "ctrl+shift+left", "alt+enter", "f1", "escape", "?", "space": any modifier order, case-insensitive. 1 on success. A chord never carries
 * an Unknown key's raw bytes. "any" is the last key of a sequence that takes whichever character is typed: `"app.mark": ["m any"]`
 * is m, then a letter, which `rolltui_windows_any_key` gives. It is refused alone and anywhere but last. */
int rolltui_chord_parse(const char* text, size_t len, RolltuiChord* out);
#define ROLLTUI_CHORD_ANY 0x10FFFEu /* the `ch` of an `any` chord, whose key is ROLLTUI_KEY_UNKNOWN */

void rolltui_bindings_free(RolltuiBindings* b);

RolltuiBindings* rolltui_bindings_clone(const RolltuiBindings* b);

/* A BORROW, valid until the table next changes. */
int rolltui_bindings_has(const RolltuiBindings* b, const char* action, size_t len);

/* A SEQUENCE in a bindings file is up to this many chords, apart by spaces: `"g g"`, `"ctrl+x ctrl+s"`. */
#define ROLLTUI_SEQUENCE_MAX 4
/* Where a key leaves a sequence: no part of one, on the way to one, or the end of one (`rolltui_windows_app_key`). */
#define ROLLTUI_SEQUENCE_NONE 0
#define ROLLTUI_SEQUENCE_PREFIX 1
#define ROLLTUI_SEQUENCE_DONE 2

/* The action of `scope` this chord serves, or NULL: a row nothing declares never answers, nor does a chord the ACTIVE protocol cannot
 * deliver (both are kept in the table and written back, so neither may claim a key). A BORROW, as above. */
const char* rolltui_bindings_action_for(const RolltuiBindings* b, const RolltuiChord* k, const char* scope,
                                        size_t scope_len, size_t* out_len);

/* THE OTHER DIRECTION: which chords run an action, so a host's status line or help text says "F2 settings" from the live table rather
 * than a string that goes stale on a rebind. `rolltui_chord_display` spells a chord the way the help popup does. */
size_t rolltui_chord_display(const RolltuiChord* k, char* out, size_t cap);
size_t rolltui_bindings_chord_count(const RolltuiBindings* b, const char* action, size_t len);
/* Chord `i` of the row, into `out`. 0 when there is none. */
int rolltui_bindings_chord_at(const RolltuiBindings* b, const char* action, size_t len, size_t i, RolltuiChord* out);

/* Binds `chord`; a chord already bound to another action of the same scope MOVES, and
 * `moved_from` (a BORROW, valid until the next change) says which. Refused (0) for an
 * undeclared action and for a chord the Enter rule protects. */
int rolltui_bindings_bind(RolltuiBindings* b, const char* action, size_t len, const RolltuiChord* chord,
                          const char** moved_from, size_t* moved_len);

void rolltui_bindings_report_release(RolltuiBindingsReport* r); /* frees everything; zeroes it */

/* "" when clean, else `error`, else "bad: x; conflict: y; chord: z; undeliverable: w; unknown action: u; unknown: k" joined in that
 * order. Replaces `*out`. PUBLIC: a host that loads its own bindings FILE tells its developer what the file asked for that this app
 * cannot give. */
void rolltui_bindings_report_summary(const RolltuiBindingsReport* r, RolltuiStr* out);

/* ADDS a file's rows to `b`, which the caller constructs first (seeded with the library's own actions), so an action the caller already
 * declared is never re-added and its row, if the file has one, gains chords. `deliver` is the protocol every chord in the file is
 * checked against. `is_library`/`reason` are the two vocabulary questions above, asked back through callbacks. Returns 0 only when the
 * file is fundamentally unusable (not a JSON object, or no "bindings" object): `report->error` says which and `b` is left as passed in.
 * A lesser problem is reported and `b` still gains what the file was good for. `report` is reset on every call. */
int rolltui_bindings_load_json(RolltuiBindings* b, const char* text, size_t len, unsigned char deliver_protocol,
                               RolltuiScopeFn is_library, void* library_ctx, RolltuiReasonFn reason,
                               void* reason_ctx, RolltuiBindingsReport* report);

/* Serialises to TEXT: {"name", "bindings": {action: [chord, ...], ...}}, 2-space indented with a trailing newline. REPLACES `*out`.
 * Every row is written, declared or not: the file is the whole domain, and an undeclared row's chords must round-trip. */
void rolltui_bindings_dump_json(const RolltuiBindings* b, const char* name, size_t name_len, RolltuiStr* out);

/* Is `scope` one the LIBRARY defines? True for exactly the scopes of the closed table above (`input`, `transcript`, `menu`, `edit`,
 * `stack`). Matches `RolltuiScopeFn`, so it can be passed straight to `rolltui_bindings_undeclare_others`. */
int rolltui_bindings_library_scope(void* ctx, const char* scope, size_t len);

/* AUTHORITATIVE over every non-library scope: after this call the declared non-library actions are EXACTLY `declared` + `tools`, so
 * loading another screen makes the last one's inert (merely ADDING would leave a key working because of a layout no longer running).
 * The order inside is load-bearing and is why these are one call: the suggestions go in FIRST, because declaring an action creates an
 * empty row for it, and a suggestion made afterwards would see that row and decline every time. */
void rolltui_bindings_declare(RolltuiBindings* b, const RolltuiLayoutAction* declared, size_t declared_n,
                              const RolltuiToolAction* tools, size_t tools_n);

/* THE SHIPPED DEFAULT TABLE: the embedded `default` bindings file, parsed and validated once and cached for the life of the process
 * (released by `rolltui_shutdown`). BORROWED: never freed, never mutated by the caller; clone it to edit. It ABORTS on two build
 * mistakes, because both are the LIBRARY's error: (1) the file does not load cleanly, checked against the WEAKEST key protocol (Legacy),
 * since the shipped file belongs to every host on every terminal (a chord that only works on kitty is fine in a user's file and a build
 * mistake here); (2) it binds an action no shipped layout declares (a key every host advertises and cannot press; a mounted tool's chords
 * come from the tool). */
/* A table SEEDED with the library's own: the Enter rule set, and all 59 closed actions declared. This is what every host starts from,
 * and what `rolltui_bindings_load_json` means by "the caller constructs first": it ADDS a file's rows to a table that already knows the
 * library's vocabulary, so a file naming `input.submit` is recognised rather than reported unknown. `rolltui_bindings_new` stays the
 * EMPTY one, for a test that wants to watch rows appear. */
RolltuiBindings* rolltui_bindings_new_seeded(void);

const RolltuiBindings* rolltui_bindings_default(RolltuiContext* c);

/* ---- the keys sheet: `keys` and `help` ------------------------------------------------------
 * `keys` and `help` in a layout are one sheet: every key under its scope's heading, the program's own first, each row what the
 * action does and its keys. It shows the table the stack routes keys through, so a screen's own `app.*` actions are in it because the
 * screen declared them. This call says WHERE AN EDIT GOES: the Bindings preset store the app already keeps. Every keys sheet on the
 * screen, `help` included, then edits into it; without it the sheet only shows the keys. Only what the person changed is written to
 * the store's working copy, so an app that watches `rolltui_bindings_store_version` picks a rebinding up the way it picks up a
 * loaded preset. */
void rolltui_windows_set_bindings_store(RolltuiWindows* w, const char* content, size_t len,
                                        RolltuiBindingsStore* store, int persist);

/* Whether the sheet loads and saves presets (on by default). An app with exactly one binding table turns this off and keeps editing,
 * undo and redo, and reset. `content` as above. */
void rolltui_windows_set_keys_editor_show_presets(RolltuiWindows* w, const char* content, size_t len, int show);

void rolltui_bindings_preset_report_release(RolltuiBindingsPresetReport* r); /* frees everything; zeroes */

/* `_clean` is a JUDGEMENT about the library's own data, so it is the library's: it returns 1 when the report has nothing to report.
 * `_summary` APPENDS the sentence `rolltui_preset_report_summary` would, with bindings' fields already wired. */
int rolltui_bindings_preset_report_clean(const RolltuiBindingsPresetReport* r);

void rolltui_bindings_preset_report_summary(const RolltuiBindingsPresetReport* r, RolltuiStr* out);

/* The key list for ONE scope, appended as "<indent><chord-or-(unbound)><pad><description>\n" rows, the chord column aligned to the
 * widest (capped at 22, minimum column 12). Undeliverable chords are left out, which is why this takes the live table. `actions` /
 * `action_lens` / `actions_n` name the rows and their order when non-empty; otherwise every action of `scope` in table order. `indent`
 * prefixes every row. */
void rolltui_help_scope_lines(const RolltuiBindings* b, const char* scope, size_t slen, const char* const* actions,
                              const size_t* action_lens, size_t actions_n, const char* indent, size_t indent_len,
                              RolltuiStr* out);

/* The whole help document: `lead`, then one "<scope>:\n" section per scope with that scope's
 * rows under it, then `note`. APPENDS to `out`. */
void rolltui_help_document(const RolltuiBindings* b, const char* lead, size_t lead_len, const char* const* scopes,
                           const size_t* scope_lens, size_t scopes_n, const char* note, size_t note_len,
                           RolltuiStr* out);

/* ========================================================================================
 * theme — a host holds a style table
 * ======================================================================================== */

/* THE SETTING layer: what a preset file and `--color-depth` / `--theme-mode` accept: every name `style`'s `ROLLTUI_DEPTH_LIST`/
 * `ROLLTUI_MODE_LIST` declare (BOOK 1 — CORE) PLUS "auto" (resolve it, do not store it). A host may narrow what IT accepts through
 * the parse callbacks. */
int rolltui_color_depth_setting_valid(const char* s, size_t len);

int rolltui_theme_mode_setting_valid(const char* s, size_t len);

/* A role or effect-state NAME TABLE, handed to the loader or dumper once per call. Every name is NUL-terminated and every pointer a
 * BORROW for the one call.
 *   role_names / role_count     the role declaration order; `out_styles` is filled positionally against it.
 *   text_role                   the ordinal "text" resolves to: the role every other inherits from.
 *   state_names / state_count   the effect-state order; index 0 ("none") never matches a file's "effects" key.
 *   fallback_effect_role        the role an effect spec with none of its own picks, handed to `rolltui_effect_map_new` once. */
typedef struct RolltuiThemeVocab {
  const char* const* role_names;
  size_t role_count;
  size_t text_role;
  const char* const* state_names;
  size_t state_count;
  unsigned char fallback_effect_role;
} RolltuiThemeVocab;

/* ---- the load report ---------------------------------------------------------------------- */
typedef struct RolltuiThemeReport {
  RolltuiStr error; /* non-empty: the file was unusable */
  RolltuiStr* missing_roles; size_t missing_roles_n, missing_roles_cap; /* GROWING AMORTISED */
  RolltuiStr* unknown_keys;  size_t unknown_keys_n,  unknown_keys_cap;  /* GROWING AMORTISED */
  RolltuiStr* bad_values;    size_t bad_values_n,    bad_values_cap;    /* GROWING AMORTISED */
  /* WHERE THE FILE'S OWN CLASSIFICATION DISAGREES WITH THE COLOURS (growing amortised). A theme states its contrast and colour-vision
   * classification in `meta.badges` and the loader recomputes it: one sentence per claim that does not hold, per computed badge the file
   * does not claim, and for a declaration that is missing or the wrong shape. Its own list, not `bad_values`: a stale badge is a problem
   * with the DECLARATION, and the theme still loads and draws. The renderer never reads the declaration. */
  RolltuiStr* badge_mismatches; size_t badge_mismatches_n, badge_mismatches_cap;
} RolltuiThemeReport;

/* COLORTERM=truecolor|24bit -> TrueColor; TERM containing "256color" -> Ansi256; TERM=dumb -> Mono; TERM empty -> Mono, except on
 * Windows, where it is Windows' own console and TrueColor; else Ansi16. `force` (ROLL_COLOR_DEPTH) wins when set and valid. Any
 * argument may be NULL. */
unsigned char rolltui_detect_color_depth(const char* colorterm, const char* term, const char* force);

/* The mode a background implies: relative luminance (sRGB linearised, Rec. 709 weights)
 * above 0.5 is light, anything else — including a colour that is not rgb — is dark. */
unsigned char rolltui_mode_for_background(RolltuiStyleColorRaw bg);

/* THE LIBRARY'S OWN role and effect-state name table: the default for a host with no roles of its own (the parameter stays on every
 * function that takes one). BORROWS static storage. */
const RolltuiThemeVocab* rolltui_theme_default_vocab(void);
/* THE SESSION'S vocabulary: the library's roles and states, then every state the host registered (`rolltui_effect_state_register`),
 * in order. A BORROW valid until the next state registration; with nothing registered it IS the default. Hand this, not the default,
 * to `rolltui_theme_load` and `rolltui_theme_effects_merge` in a host with states of its own, or a theme file naming them reports each
 * as an unknown key. */
const RolltuiThemeVocab* rolltui_theme_vocab(const RolltuiContext* c);

/* Fills `styles[0..role_count)` (CALLER-FILLED, no allocation) for the named built-in theme and returns a freshly built, OWNED effect
 * map the caller adopts or frees with `rolltui_effect_map_free`. NULL, with `styles` untouched, when the name is unknown OR `role_count`
 * does not match the library's role table (a mismatch reads as "no such theme" rather than writing past the caller's array). */
RolltuiEffectMap* rolltui_theme_builtin_fill(const char* name, size_t name_len, RolltuiStyle* styles,
                                             size_t role_count);

/* Frees everything and zeroes the struct: safe on an already-zeroed one and on repeated calls. Zero-initialise a fresh one
 * (`RolltuiThemeReport r = {0};`) before first use. */
void rolltui_theme_report_release(RolltuiThemeReport* r);

/* ---- the loader ----------------------------------------------------------------------------
 * Loads a theme from a parsed TREE (the caller parsed the file with `rolltui_json_parse`, or already held one), minus "meta" and the
 * theme object itself. `report` is RESET by this call whether it succeeds or fails. Returns NULL only when `root` is not a usable
 * theme object at all (`report->error` says why); `out_styles` and `out_name` are untouched then. Every other problem still yields a
 * usable theme: `out_styles[0..vocab->role_count)` is filled in full (the "text" style substituted for any role the file did not
 * define, `report->missing_roles` naming each), `out_name` gets the theme's "name" ("unnamed" when absent), and the return is a
 * freshly built, OWNED, non-NULL effect map (empty, a still UI, for a file with no usable "effects"), with every problem in `report`. */
/* ADDS the `effects` of a JSON document to an existing map (`{ "effects": { state: spec } }`, or a bare state -> spec object), resolving
 * state and role names through `vocab` and widening the map to the vocabulary's state count first. This is how an APP ships the motion
 * for its own states as a file of its own while the colours stay the person's theme. Every borrowed `RolltuiEffectSpec*` into `map` is
 * invalidated. Returns 1 when the text parsed; `report` carries unparseable text as `error`, an unknown state or key as `unknown_keys`,
 * a bad value as `bad_values`. */
int rolltui_theme_effects_merge(RolltuiEffectMap* map, const char* text, size_t len, const RolltuiThemeVocab* vocab,
                                RolltuiThemeReport* report);

RolltuiEffectMap* rolltui_theme_load(const RolltuiJsonValue* root, int mode, const RolltuiThemeVocab* vocab,
                                     RolltuiStyle* out_styles, RolltuiStr* out_name, RolltuiThemeReport* report);

/* Frees everything and zeroes the struct: safe on an already-zeroed one and on repeated calls (the "reset, not just release" contract of
 * every report here). */
void rolltui_theme_preset_report_release(RolltuiThemePresetReport* r);

/* Parses a preset file's top level from an already-parsed tree: "name"/"preset" (a string when present, else a bad value; the VALUE is
 * the store's business), "mode"/"depth" (checked with `mode_valid`/`depth_valid`; both start "auto" and keep whatever `out_mode`/
 * `out_depth` held when the key is absent or fails its check), "colours" (required), and "layout" (a leftover key: a NOTE naming it, not
 * an error). A root that is not a usable object is `report->error`, checked first. Unknown keys are reported, not rejected. Colour
 * validation runs at BOTH modes, so a role wrong only in one variant is still caught: `report->colours` is dark's report plus light's
 * bad values not already in dark's, and only DARK's success or failure decides the return value.
 * Returns 1 when `root` is a usable preset (`report` may still carry notes, bad values and unknown keys), 0 when it is not
 * (`report->error` says which; `*out_colours` is NULL). `report` is reset by this call. */
int rolltui_theme_preset_parse(const RolltuiJsonValue* root, const RolltuiThemeVocab* vocab,
                               RolltuiThemePresetValidFn mode_valid, RolltuiThemePresetValidFn depth_valid,
                               RolltuiStr* out_mode, RolltuiStr* out_depth, const RolltuiJsonValue** out_colours,
                               RolltuiThemePresetReport* report);

/* Builds a preset file's tree: {"name","mode","depth","colours"}. TAKES OWNERSHIP of `colours` (folded into the result, as
 * `rolltui_json_set` does). OWNED: the caller frees the result with `rolltui_json_free`. Never sets "preset": that key belongs to the
 * store's origin writer. */
RolltuiJsonValue* rolltui_theme_preset_to_json(RolltuiJsonValue* colours, const char* mode, size_t mode_len,
                                               const char* depth, size_t depth_len, const char* name,
                                               size_t name_len);

/* ---- the theme store: USE — create it, read it, switch presets, save one, add one ----------
 * The whole surface a host needs. Saving the working copy under a name and adding a file someone sent are here because a host offers
 * both — roll's `theme save`/`theme add` and its settings menu's save-as. Editing a preset's own CONTENT in place — `_edit`,
 * `_set_working` — is `rolltui_studio.h`'s, reached only by rolltui's own studio and its editors. */
typedef struct RolltuiThemeStore RolltuiThemeStore;

/* OWNED: `_new`/`_free`, `_free` a no-op on NULL. `dir` is where this person's own presets and working copy live; `shipped_dir`, when
 * `may_write_shipped`, is where the studio may ALSO write a preset that ships with the library (`""` refuses that entirely — every
 * host but the studio passes it). `c` reaches the session's own domain descriptor: every registry and cache this library keeps lives
 * on a context and is released by name when it is freed, even here, where nothing today configures a theme differently per session —
 * configuration belongs to the thing it configures, not to a second copy of it a host's own descriptor could silently desync from. */
RolltuiThemeStore* rolltui_theme_store_new(RolltuiContext* c, const char* dir, size_t dir_len, int may_write_shipped,
                                           const char* shipped_dir, size_t shipped_dir_len);
void rolltui_theme_store_free(RolltuiThemeStore* s);

/* Loads the autosaved working copy, or "default" when there is none or it does not parse — `report` says which and is RESET by
 * this call. Call once, right after `_new`. */
void rolltui_theme_store_start(RolltuiThemeStore* s, RolltuiThemePresetReport* report);
/* THE FILES READ AGAIN when another program changed them (the studio, an editor): the working copy as it now is, or, while it has no
 * edits of its own, the preset it follows. 1 when it was read again, its version moved as an edit's would; `report` RESET says how
 * it went. This store's own writes never count. `rolltui_run_watch_stores` calls it when the files change. */
int rolltui_theme_store_refresh(RolltuiThemeStore* s, RolltuiThemePresetReport* report);

/* A BORROW of the store's own working value, valid until the next call that changes it (`_load`, `_set_mode`, `_set_depth`, or —
 * `rolltui_studio.h` — `_edit`). Never yours to free. NULL only when `s` is NULL. For a host whose every change to this store happens
 * on the thread that reads it; a host that changes it from another thread holds `_copy_working` instead. */
const RolltuiThemePresetValue* rolltui_theme_store_working(const RolltuiThemeStore* s);

/* An OWNED copy of the working value, taken under the store's lock — what a host that renders on one thread while another changes
 * the store holds, since a borrow can be freed out from under it by a load on the other thread. Take a fresh one whenever
 * `_version` moves, which every change does, a live edit's preview included, so the copy never goes stale. Free it with
 * `rolltui_theme_preset_value_free`. NULL only when `s` is NULL. */
RolltuiThemePresetValue* rolltui_theme_store_copy_working(const RolltuiThemeStore* s);

int rolltui_theme_store_modified(const RolltuiThemeStore* s);

/* "<origin>", or "<origin> (modified)" once the working copy differs from what it was loaded from. REPLACES `*out`. */
void rolltui_theme_store_label(const RolltuiThemeStore* s, RolltuiStr* out);

unsigned long long rolltui_theme_store_version(const RolltuiThemeStore* s);

/* A BORROW of the origin's name alone, no "(modified)" suffix — `_label` composes that for a status line; this is for a
 * caller that needs the plain name, e.g. to compare against a listed preset. Valid until the next call that changes it. */
const char* rolltui_theme_store_origin(const RolltuiThemeStore* s, size_t* len);

/* The shipped ones first ("default" ahead of the rest), then this person's own that do not shadow a shipped name. REPLACES `*out`. */
void rolltui_theme_store_list(const RolltuiThemeStore* s, RolltuiPresetListRaw* out);

/* A named preset's value WITHOUT switching to it — reading a theme's badges before choosing it, say. OWNED: free the result with
 * `rolltui_theme_preset_value_free`. NULL when no preset has that name (`report` says why). */
RolltuiThemePresetValue* rolltui_theme_store_get(const RolltuiThemeStore* s, const char* name, size_t len,
                                                 RolltuiThemePresetReport* report);

/* Switches the working copy to a different EXISTING preset by name — picking a theme, not authoring one. Light-or-dark and colour
 * depth are the person's and are carried over, not the preset's to change (`rolltui_theme_store_set_mode`/`_set_depth` are the only
 * way to change them). Returns 1 on success; `report` says what happened either way and is RESET by this call. `persist` 0 fills the
 * working copy without writing it, for a run that must not touch disk. */
int rolltui_theme_store_load(RolltuiThemeStore* s, const char* name, size_t len, RolltuiThemePresetReport* report,
                             int persist);

/* Saves the CURRENT working value under a new name, which becomes the origin. Always autosaves afterwards (a save-as is an explicit
 * write, unlike `_load`, which respects `persist`): there is no `persist` parameter because no caller wants 0. Returns one of the
 * ROLLTUI_SAVE_* codes; `err` (may be NULL) carries the sentence for every result but SAVED. */
int rolltui_theme_store_save_as(RolltuiThemeStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);

/* Copies an external FILE in as a new preset (a theme someone sent you) — additive, never replacing an existing one. Named by
 * `as`, or by the file's own stem when `as` is empty. Same ROLLTUI_SAVE_* codes as `_save_as`, plus BAD_NAME for a file that does
 * not parse as a theme. */
int rolltui_theme_store_add(RolltuiThemeStore* s, const char* path, size_t path_len, const char* as, size_t as_len,
                            RolltuiStr* err);

/* Light-or-dark and colour depth: the person's own settings, carried beside a preset's colours rather than part of them (every preset
 * keeps whichever the person already had). `mode`: "auto"|"dark"|"light"; `depth`: "auto"|"truecolor"|"256"|"16"|"mono". */
void rolltui_theme_store_set_mode(RolltuiThemeStore* s, const char* mode, size_t len, int persist);
void rolltui_theme_store_set_depth(RolltuiThemeStore* s, const char* depth, size_t len, int persist);

/* The two paths this store's files live at. `name` NULL (or `len` 0): the working copy's path. Otherwise a named preset's —
 * shipped or this person's own, whichever `_load` would follow. REPLACES `*out`. */
void rolltui_theme_store_path(const RolltuiThemeStore* s, const char* name, size_t len, RolltuiStr* out);

/* The shipped ones — no store needed, `c` alone: every store built against the same context clones from the same embedded table. */
int rolltui_theme_is_shipped(RolltuiContext* c, const char* name, size_t len);
void rolltui_theme_shipped_names(RolltuiContext* c, RolltuiStrListRaw* out);
/* A BORROW, valid until `c` is freed. NULL when `name` is not shipped. */
const RolltuiThemePresetValue* rolltui_theme_shipped(RolltuiContext* c, const char* name, size_t len);

/* ---- the `theme` widget kind's one call -----------------------------------------------------
 * An app gets a theme editor by NAMING `theme` in a layout and binding a key to whatever holds it; this is the only code it writes.
 * The theme is the APP's: the style table a frame is drawn with is passed into `rolltui_windows_draw` by the host, so no widget can
 * reach it. What the app hands over is the Theme preset store it already keeps: the editor loads its working copy, lists its presets
 * in the Load choice and writes every commit back, so an app that watches `rolltui_theme_store_version` picks a theme edit up the
 * way it picks up a loaded theme. An app with no store opens one.
 * `content` is the layout's own string for the window ("theme", or "theme:<anything>"); NULL or 0 means "theme". The widget is
 * CREATED if this screen has none yet, so a host may wire the store before its first draw. `persist` non-zero autosaves each commit
 * (what a session wants and a golden frame does not). The store is BORROWED and must outlive `w`. */
void rolltui_windows_set_theme_store(RolltuiWindows* w, const char* content, size_t len, RolltuiThemeStore* store,
                                     int persist);

void rolltui_theme_preset_value_release(RolltuiThemePresetValue* v); /* frees `colours`; zeroes */

/* The full free for a value `_get()` handed back — `v` itself included, the way `rolltui_layout_free`/`rolltui_bindings_free`
 * already do for their own domains. `_release` above only clears what `v` owns, for a caller holding the struct itself (by value
 * or as a member); this is for a caller holding the heap pointer `_get()` returned. */
void rolltui_theme_preset_value_free(RolltuiThemePresetValue* v);

/* `_clean` is a JUDGEMENT about the library's own data ("does a missing role make a theme preset unclean?"), so it is the library's:
 * it returns 1 when the report has nothing to report. `_summary` APPENDS the sentence `rolltui_preset_report_summary` would, with
 * theme's fields already wired. */
int rolltui_theme_preset_report_clean(const RolltuiThemePresetReport* r);

void rolltui_theme_preset_report_summary(const RolltuiThemePresetReport* r, RolltuiStr* out);

/* ========================================================================================
 * layout_tree — the tree inside RolltuiLayout: a public type, so its lifecycle is public
 * ======================================================================================== */

#ifdef __cplusplus
namespace rolltui {
enum class Border : unsigned char { None, Single, Rounded, Double, Heavy, Hidden, QuadrantInside, QuadrantOutside };
enum class Anchor : unsigned char {
  TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight
};
// Declared, not defined: an opaque enum declaration with a fixed underlying type is a complete type, which is all a member needs.
enum class Role : unsigned char;

}  // namespace rolltui
#endif

/* floor(fraction * extent + 1e-6) + cells */
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

/* `std::optional<Dim>` in C++ was a flag the language supplied; here it is a flag, said out loud. The C++ methods keep `if (p.min_w)`
 * and `*p.min_w` compiling. */
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

/* How much of the parent split's axis a node takes. RolltuiSplitSizeRaw: the plain data.
 * `rolltui_cpp.h` declares `class RolltuiSplitSize : public RolltuiSplitSizeRaw` reclaiming the
 * name, with static fixed()/filling(); `operator==` is free. */
typedef struct RolltuiSplitSizeRaw {
  unsigned char fill ROLLTUI_DEFAULT(1); /* a share of the remainder, by `weight` */
  int weight ROLLTUI_DEFAULT(1);
  RolltuiDim dim; /* when !fill */
} RolltuiSplitSizeRaw;

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
  /* Columns kept clear of the screen's left and right edges when `clamp` fits this popup on screen: 0 (the default) clamps flush to the
   * edge, as a docked editor panel wants; a popup that centres over the whole screen wants a gutter so it never grows (via `min_w`) to
   * either edge. Width only. */
  unsigned char edge_margin ROLLTUI_DEFAULT(0);
  RolltuiOptDim min_w, min_h, max_w, max_h;
} RolltuiPlacement;

struct RolltuiLayoutNode;

/* A node's OWNED children. Pointers, not values, so a child's address never moves and a `const RolltuiLayoutNode*` handed out by
 * `place()` or `find()` stays valid across an edit. */
/* ---- THE LAYOUT FAMILY IS OPAQUE ------------------------------------------------------------
 * A layout, its layers, the nodes of its split tree and the three lists that hold them are HANDLES here and structures in
 * `rolltui/c/rolltui_layout_tree.h` (the library's own). Opaque now with a door opened later is reversible; transparent now and
 * opaque later is a break. The eight doors declared beside this module's own functions, below, are the whole of what consumers do
 * with a layout: hand the base to a window stack, declare the actions, show the name and minimum size, and read an id off a node or
 * layer the library handed back. If a real need appears, add a door and say who forced it. The one consumer that walks and mutates a
 * tree is the studio's layout editor, which reaches the structures through the internal header by name. */

/* PLAIN DATA, emitted per node per frame: it borrows the node it describes and is what a host reads to draw. `node` is a BORROW valid
 * while the tree it came from is not edited. */
typedef struct RolltuiResolvedNode {
  const RolltuiLayoutNode* node ROLLTUI_DEFAULT(nullptr);
  RolltuiRect outer; /* the node's box before clipping to the frame */
  RolltuiRect inner; /* outer minus the border, clipped — what a split divides / a slot draws in */
  unsigned char focused ROLLTUI_DEFAULT(0);
  /* THE POINTER RESTS HERE: 1 on the one window a press at the pointer would reach, while the terminal reports hover
   * (`RolltuiTerminalOptions::hover`), with the pointer's cell in frame coordinates. A widget draws what a click there would act on
   * in the `hover` role and moves nothing: the keyboard's cursor stays where it is. 0 on every other window, and on every window
   * under a popup that is modal or closes on a press outside it. */
  unsigned char hover ROLLTUI_DEFAULT(0);
  int hover_x ROLLTUI_DEFAULT(0), hover_y ROLLTUI_DEFAULT(0);
  size_t layer ROLLTUI_DEFAULT(0);
} RolltuiResolvedNode;

/* ---- the text forms ---------------------------------------------------------------------------- */
/* "32" | "50%" | "100% - 32", into a caller's buffer. */
#define ROLLTUI_DIM_STRING_MAX 64

/* Called once per node, in TREE ORDER (a container precedes its children). The caller
 * decides where they go — a vector, a filter, a single hit test. */

typedef void (*RolltuiResolvedSink)(void* ctx, const RolltuiResolvedNode* rn);

/* THE THREE ROLES A COMPOSE NEEDS, handed in as bytes. This file names none of them. */
typedef struct RolltuiLayoutRoles {
  unsigned char border;
  unsigned char border_active;
  unsigned char title;
  unsigned char overlay;
  /* A SCREEN TOO SMALL: its size in `error`, the size needed in `ok`; 0 for either draws it in `title`. */
  unsigned char error ROLLTUI_DEFAULT(0), ok ROLLTUI_DEFAULT(0);
} RolltuiLayoutRoles;

/* The host fills a window's content slot into `rn->inner` (already clipped). NULL draws
 * nothing, which is what a golden-frame harness wants. */
typedef void (*RolltuiSlotFn)(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f);

/* WORKING MEMORY THE CALLER OWNS: one layer's resolved windows, and the draw scratch a title needs. One buffer per ROLE, so the
 * compose's nodes and the text walk's clusters cannot alias. */
typedef struct RolltuiComposeScratch RolltuiComposeScratch;

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

/* Why a registration was refused, so a host can say it in words. 0 is accepted. */
#define ROLLTUI_REGISTER_OK 0

#define ROLLTUI_REGISTER_EMPTY 1

#define ROLLTUI_REGISTER_HAS_COLON 2

#define ROLLTUI_REGISTER_IS_LIBRARY 3    /* rung 1 is never shadowed */

#define ROLLTUI_REGISTER_RULE_DIFFERS 4  /* already registered, with another source rule */

/* PLAIN DATA: the kind's NAME and its source, the two halves of `kind[:source]`. Every member has correct value semantics (`RolltuiStr`'s),
 * so this type declares no constructor, destructor or assignment and `operator==` is `= default`. An EMPTY `kind` names nothing:
 * `rolltui_content_parse` fills one and `rolltui_widget_kind_resolve` answers for one; neither invents a default kind. */

#define ROLLTUI_CONTENT_PROBLEM_NONE 0

#define ROLLTUI_CONTENT_PROBLEM_UNKNOWN_KIND 1

#define ROLLTUI_CONTENT_PROBLEM_MISSING_SOURCE 2

#define ROLLTUI_CONTENT_PROBLEM_FORBIDDEN_SOURCE 3

/* Role names: ask back rather than carry a table. `role_from_name` returns 1 and fills `*out` for a recognised name; `role_name` returns
 * the name's length, writing at most `cap` bytes plus a NUL into `out` (nothing when the ordinal is unknown to the caller): a caller-owned
 * buffer, since a role's name has no storage on this side of the call to borrow from. */
typedef int (*RolltuiRoleFromNameFn)(void* ctx, const char* name, size_t len, unsigned char* out);

typedef size_t (*RolltuiRoleNameFn)(void* ctx, unsigned char role, char* out, size_t cap);

#define ROLLTUI_ROLE_NAME_MAX 32 /* longest shipped role name plus room */

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

/* ---- the layout report: unknown keys and bad values are problems, notes are not -----------
 * Transparent like every report here: `RolltuiStr` values in growing amortised arrays. Zero-initialise before use. */
typedef struct RolltuiLayoutReport {
  RolltuiStr error; /* non-empty: the file was unusable */
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
  /* Things the loader DID that the file did not ask for and a host may want to say once (today: a file declaring no "actions" is given
   * the shipped default's). Not part of "clean", because none of it is a problem. */
  RolltuiStr* notes;
  size_t notes_n, notes_cap;
} RolltuiLayoutReport;

/* One entry of a layout file's "actions" object: a name and its English description, "the actions THIS SCREEN emits". Also
 * `RolltuiLayout::actions`' element type. It is the layout FILE's vocabulary, kept apart from Bindings' so a layout file does not
 * depend on it. */
typedef struct RolltuiLayoutAction {
  RolltuiStr name;
  RolltuiStr description;
} RolltuiLayoutAction;

/* The parsed layout: a TRANSIENT carrier, scoped to one `rolltui_load_layout*` call and never retained. It stays a separate struct
 * because the loader's bookkeeping (`actions_cap` growing across a parse that has not yet decided the file is usable) is not
 * `RolltuiLayout`'s API. The conversion right after a load MOVES the tree, since the carrier is about to be released. */

/* ---- the layout itself: the ENDURING value a host holds ------------------------------------
 * `rolltui::Layout` IS this struct. Every member has correct value semantics (`RolltuiStr`, the two lists above, `RolltuiLayer`), so
 * it declares no constructor, destructor or copy/move; `popup()` is the one convenience member. */

/* THE ACTION NAMES, handed in — this file knows the RULES and none of the words. `next_tab` and `prev_tab` show the focused window's
 * next or previous tab, `maximize` makes the focused window fill its layer and puts it back; NULL leaves the rule out. */
typedef struct RolltuiStackActions {
  const char* close_popup;
  const char* focus_next;
  const char* focus_prev;
  const char* next_tab;
  const char* prev_tab;
  const char* maximize;
  const char* focus_left;
  const char* focus_right;
  const char* focus_up;
  const char* focus_down;
  const char* focus_number;
  const char* half;
} RolltuiStackActions;

#define ROLLTUI_ROUTE_DELIVER 0

/* The close key on a popup — or a press outside one whose layout says `"dismiss": true`. */
#define ROLLTUI_ROUTE_CLOSED_POPUP 1

#define ROLLTUI_ROUTE_FOCUS_MOVED 2

#define ROLLTUI_ROUTE_DROPPED 3
/* The close key, or a press outside a dismissing popup, closed ONE LEVEL inside the focused
 * window's widget (a dropdown, a menu level) and the popup stays; `window` names that window. */
#define ROLLTUI_ROUTE_CLOSED_LEVEL 4
/* A press on a window's tab strip, or a tab or maximise key, changed what the screen shows; `window` names the window. */
#define ROLLTUI_ROUTE_LAYOUT_CHANGED 5
/* A press on a label in a window's title (`rolltui_window_stack_set_title_parts`); `window` names the window and
 * `rolltui_window_stack_pressed_label` gives the label's action. */
#define ROLLTUI_ROUTE_LABEL 6

/* WHO ANSWERS "CLOSE ONE LEVEL" — the shape; the setter is with the stack's other setup. */
typedef int (*RolltuiStackLevelFn)(void* ctx, const char* window, size_t len);

/* ---- the layout handle and its doors -------------------------------------------------- */
/* OWNED: `_new` makes an empty one, `_free` is a no-op on NULL, `_clone` deep-copies.
 * A layout also arrives OWNED from `rolltui_load_layout_text`, and BORROWED from
 * `rolltui_layout_builtin` and the Layout preset store. */
RolltuiLayout* rolltui_layout_new(void);
void rolltui_layout_free(RolltuiLayout* l);
RolltuiLayout* rolltui_layout_clone(const RolltuiLayout* l);

/* DOOR 1 — the base layer, to hand to `rolltui_window_stack_set_base`. Forced by all four
 * consumers; it is the first thing every one of them does with a layout. BORROWED. */
const RolltuiLayer* rolltui_layout_base(const RolltuiLayout* l);

/* DOOR 2 — the screen's declared actions, for `rolltui_bindings_declare` and an app profile.
 * Forced by all four consumers. BORROWED; `*n` is the count. */
const RolltuiLayoutAction* rolltui_layout_actions(const RolltuiLayout* l, size_t* n);

/* DOOR 3 — the minimum terminal size this screen states. Forced by roll (its narrow-terminal
 * fallback picks another layout below it) and by paint (its app profile publishes it). */
void rolltui_layout_min_size(const RolltuiLayout* l, int* w, int* h);
/* 1 when the layout is drawn INLINE, below the prompt in the normal screen (`"inline": true`): a host makes its terminal with
 * `RolltuiTerminalOptions::inline_rows` for it. */
int rolltui_layout_inline(const RolltuiLayout* l);

/* DOOR 4 — the screen's name. Forced by paint, which draws it in its status line. BORROWED. */
const char* rolltui_layout_name(const RolltuiLayout* l, size_t* len);

/* DOOR 5: a declared popup by id, or NULL. `rolltui_window_stack_push_popup` takes the layout and the id directly, so pushing one needs
 * no layer of your own. BORROWED. */
const RolltuiLayer* rolltui_layout_popup(const RolltuiLayout* l, const char* id, size_t len);

/* DOOR 8: a layer's id (a host pops until the top layer is the one it named). BORROWED. */
const char* rolltui_layer_id(const RolltuiLayer* layer, size_t* len);
/* The window the layer's file names as its first focus ("" when it names none): what a host hands focus back to after its own input is
 * done. BORROWED. */
const char* rolltui_layer_focus(const RolltuiLayer* layer, size_t* len);

/* DOOR 6 — where a layer is placed. Forced by roll: `rolltui_placement_resolve` turns it into
 * the rectangle the approval popup will take, so the input below can size itself. BORROWED. */
const RolltuiPlacement* rolltui_layer_placement(const RolltuiLayer* layer);

/* DOOR 7: a node's id, and whether it is a window rather than a row or a column. BORROWED. */
const char* rolltui_layout_node_id(const RolltuiLayoutNode* n, size_t* len);
int rolltui_layout_node_is_window(const RolltuiLayoutNode* n);

/* ---- source languages ------------------------------------------------------------------------
 * What colours a source file in a preview is a set of LANGUAGES, each a JSON file: the shipped ones, and any a person keeps in
 * `<config>/rolltui/syntax/` (one named like a shipped language replaces it). A file with a mistake is skipped, and nothing says so
 * on screen; this is how its author finds out. It loads the shipped set, the person's folder, and every file in `paths` (a language
 * being written; `path_count` may be 0), and writes into `out` what it found: how many shipped, which of the person's loaded (and
 * which replaced a shipped one), and for each file that did not, its name and why. Returns the number that did not load, so 0 is a
 * clean bill and a script can gate on it. */
size_t rolltui_syntax_check(const char* const* paths, size_t path_count, RolltuiStr* out);

/* Appends an EMPTY layer and returns it — the C's `emplace_back`. */

/* ---- layout --------------------------------------------------------------------------------*/

/* THE LIBRARY'S OWN role bytes for layout borders, so a host does not have to invent them. BORROWS static storage, valid for the life of
 * the process, never freed. A host that paints its borders from other roles still passes its own struct. */
const RolltuiLayoutRoles* rolltui_layout_default_roles(void);

/* Parses "kind[:source]". 1 on success: `*row` is the kind's row in the registry (both rungs) and `*is_host` says which rung answered
 * (0 library, 1 host). `name`/`name_len` (before the colon) and `source`/`source_len` (after; "" with a valid pointer when there was none)
 * are always filled and are BORROWS into `text`. On failure (0): `problem` says which of the three ways (never None), and `why`, cleared
 * on entry, gets the sentence.
 * `c` may be NULL, which is the LOADER's case: split the string, do not resolve a kind (every kind reads as unknown). A layout FILE is
 * parsed before a host has registered anything, so an unknown kind is not a load failure: `rolltui_windows_sync` reports it, with the
 * error panel drawn. */
int rolltui_content_parse(const RolltuiContext* c, const char* text, size_t len, size_t* row, int* is_host,
                          const char** name, size_t* name_len, const char** source, size_t* source_len,
                          unsigned char* problem, RolltuiStr* why);

/* The join rule: `kind_name`, then ":" + `source` exactly when `rule` says the colon belongs (Required always; Optional only when
 * `source` is non-empty). REPLACES `*out`. */
void rolltui_content_format(const char* kind_name, size_t kind_name_len, const char* source, size_t source_len,
                            unsigned char rule, RolltuiStr* out);

/* THE LIBRARY'S OWN role and scope callbacks, so a caller need not hand-write the table. The parameter stays on every function below
 * for a host with its own role vocabulary (an app profile's kinds): this is the default, not a policy. BORROWS static storage. */
const RolltuiLayoutHooks* rolltui_layout_default_hooks(void);

void rolltui_layout_report_release(RolltuiLayoutReport* r); /* frees everything; zeroes it */

int rolltui_layout_report_clean(const RolltuiLayoutReport* r);

/* The placement rule, in absolute coordinates (parent.x/y added), INTO a caller's rect rather than returned: `RolltuiRect` is not a
 * C++98 POD, so Clang refuses to promise an ABI for returning one from an `extern "C"` function (as for `RolltuiCell`). A rect PARAMETER
 * by value is fine. */
void rolltui_placement_resolve(const RolltuiPlacement* p, RolltuiRect parent, RolltuiRect* out);

RolltuiComposeScratch* rolltui_compose_scratch_new(void);

void rolltui_compose_scratch_free(RolltuiComposeScratch* s);

/* ---- THE SHIPPED SCREEN'S OWN ACTIONS ---------------------------------------------------
 * The "actions" object of the embedded `default` layout, parsed ONCE and cached for the life of the process (released by
 * `rolltui_shutdown`): the fallback a file that declares no actions gets, and what `rolltui_bindings_default` validates the shipped
 * key file against. BORROWS: the array is the library's and is valid until `rolltui_shutdown`, never freed by the caller. `*n` is the
 * count; the array is NULL only if the embedded file is unparseable, which is a build mistake. */
const RolltuiLayoutAction* rolltui_layout_shipped_default_actions(RolltuiContext* c, size_t* n);

/* The embedded layout file of that name, as TEXT ("" when there is none): one definition site for "which file is `default`". */
const char* rolltui_layout_builtin_json(const char* name, size_t len, size_t* out_len);

/* Where an app's OWN default files live, a different question from "may a user change it" (the preset store's rungs). Asked first; what
 * it returns is what a user's preset directory then shadows.
 * THREE RUNGS, LATER OVERRIDING EARLIER, so an embedded copy is a FLOOR that guarantees the app runs and a file on disk customises it:
 *   1. `embedded`         compiled in from the app's own files by cmake/embed_presets.cmake, matched by `kind` as the entry's stem. May be NULL.
 *   2. beside the binary  <dir of argv0>/<app>.<kind>.json
 *   3. the known folder   <ROLL_CONFIG_DIR|XDG_CONFIG_HOME/roll|$HOME/.config/roll (Windows: %APPDATA%/roll)>/rolltui/<app>/<kind>.json
 * `out` receives the winning contents. `tried` (may be NULL) receives one line per candidate, hit or miss, so a host that finds nothing can
 * say what it looked for. Returns 0 when every rung missed. */
int rolltui_app_file(const char* argv0, const char* app, const char* kind,
                     const RolltuiEmbeddedFile* embedded, size_t embedded_n,
                     RolltuiStr* out, RolltuiStr* tried);

/* ---- what an app keeps between runs --------------------------------------------------------------
 * THE STATE DIRECTORY, for what an app remembers rather than what a person configures (a history, marks, the folder it was in):
 * `ROLL_STATE_DIR`/<app>, else under a scratch home the config's own override names (`ROLL_CONFIG_DIR`/state/<app>), else
 * `XDG_STATE_HOME`/roll/<app>, else ~/.local/state/roll/<app> (Windows: %LOCALAPPDATA%\roll\<app>). Into `*out` (REPLACED), not made
 * until something is saved there; 0 where there is no file system (a web page). */
int rolltui_state_dir(const char* app, size_t len, RolltuiStr* out);
/* A STATE FILE: names and their values, as text, in a JSON object. Saving reads the file again and lays this handle's changes over
 * what is there, through a temporary file and a rename, so two instances saving in turn keep each other's names, and a name both
 * changed keeps the one saved last. After a save the handle holds what the file holds. OWNED; `_free` releases it. A file that does
 * not exist yet is an empty one; one that is not a state file is reported in `*err` (may be NULL), and the handle starts empty. */
typedef struct RolltuiStateFile RolltuiStateFile;
RolltuiStateFile* rolltui_state_file_open(const char* path, size_t len, RolltuiStr* err);
/* A BORROW valid until the next change or save; NULL when the name has no value. */
const char* rolltui_state_file_get(const RolltuiStateFile* s, const char* name, size_t len, size_t* value_len);
/* A NULL `value` removes the name. */
void rolltui_state_file_set(RolltuiStateFile* s, const char* name, size_t len, const char* value, size_t value_len);
size_t rolltui_state_file_count(const RolltuiStateFile* s);
const char* rolltui_state_file_name_at(const RolltuiStateFile* s, size_t i, size_t* len);
/* 1 when written; 0 with the reason in `*err` (may be NULL), the changes kept for the next try. */
int rolltui_state_file_save(RolltuiStateFile* s, RolltuiStr* err);
void rolltui_state_file_free(RolltuiStateFile* s); /* a no-op on NULL */

/* Parses TEXT into a layout. A JSON syntax error becomes `report->error` (NULL returned). OWNED: the caller frees the result with
 * `rolltui_layout_free`. */
RolltuiLayout* rolltui_load_layout_text(const char* text, size_t len,
                                        const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                                        const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report);

/* The library's own three stack actions, expanded from the same closed list as the per-widget tables. BORROWS static storage; a host
 * with its own words still passes its own struct. */
const RolltuiStackActions* rolltui_stack_default_actions(void);

/* ---- the layout store: USE — same shape as the theme store, minus the person's settings ------
 * A layout has none: nothing about how it is shown is the person's the way light-or-dark and colour depth are a theme's, so there is
 * no `_set_*` here. `_new` keeps `RolltuiContext*` for the same reason the theme store's does, and Layout has a second reason besides:
 * its shipped `default` screen's own declared actions are borrowed from the session's cache (`rolltui_layout_shipped_default_actions`),
 * which genuinely cannot outlive it. */
typedef struct RolltuiLayoutStore RolltuiLayoutStore;

RolltuiLayoutStore* rolltui_layout_store_new(RolltuiContext* c, const char* dir, size_t dir_len, int may_write_shipped,
                                             const char* shipped_dir, size_t shipped_dir_len);
void rolltui_layout_store_free(RolltuiLayoutStore* s);
void rolltui_layout_store_start(RolltuiLayoutStore* s, RolltuiLayoutPresetReport* report);
/* THE FILES READ AGAIN when another program changed them (the studio, an editor): the working copy as it now is, or, while it has no
 * edits of its own, the preset it follows. 1 when it was read again, its version moved as an edit's would; `report` RESET says how
 * it went. This store's own writes never count. `rolltui_run_watch_stores` calls it when the files change. */
int rolltui_layout_store_refresh(RolltuiLayoutStore* s, RolltuiLayoutPresetReport* report);
/* A BORROW of the working value, valid until the next call that changes it (`_load`, or — `rolltui_studio.h` — `_edit`), for a host
 * whose every change happens on the reading thread; across threads, `_copy_working`, as the theme store's. */
const RolltuiLayout* rolltui_layout_store_working(const RolltuiLayoutStore* s);
/* An OWNED copy, taken under the store's lock, as the theme store's. Free it with `rolltui_layout_free`. */
RolltuiLayout* rolltui_layout_store_copy_working(const RolltuiLayoutStore* s);
int rolltui_layout_store_modified(const RolltuiLayoutStore* s);
void rolltui_layout_store_label(const RolltuiLayoutStore* s, RolltuiStr* out);
unsigned long long rolltui_layout_store_version(const RolltuiLayoutStore* s);

/* A BORROW of the origin's name alone, same as the theme store's. */
const char* rolltui_layout_store_origin(const RolltuiLayoutStore* s, size_t* len);
void rolltui_layout_store_list(const RolltuiLayoutStore* s, RolltuiPresetListRaw* out);
/* OWNED: free the result with `rolltui_layout_free`. */
RolltuiLayout* rolltui_layout_store_get(const RolltuiLayoutStore* s, const char* name, size_t len,
                                       RolltuiLayoutPresetReport* report);
int rolltui_layout_store_load(RolltuiLayoutStore* s, const char* name, size_t len, RolltuiLayoutPresetReport* report,
                              int persist);
/* Save the working copy under a name, and add a file someone sent: the same contract as the theme store's. */
int rolltui_layout_store_save_as(RolltuiLayoutStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);
int rolltui_layout_store_add(RolltuiLayoutStore* s, const char* path, size_t path_len, const char* as, size_t as_len,
                             RolltuiStr* err);
void rolltui_layout_store_path(const RolltuiLayoutStore* s, const char* name, size_t len, RolltuiStr* out);
/* The shipped ones. `c` reaches the session's own descriptor, same as `_new`. */
int rolltui_layout_is_shipped(RolltuiContext* c, const char* name, size_t len);
void rolltui_layout_shipped_names(RolltuiContext* c, RolltuiStrListRaw* out);
const RolltuiLayout* rolltui_layout_shipped(RolltuiContext* c, const char* name, size_t len);

/* ========================================================================================
 * embedded — the shipped files, by name
 * ======================================================================================== */

/* BOTH FIELDS ARE BARE `const char*`, AND A C++ CALLER MUST NOT COMPARE THEM WITH `==`: `table[i].name == "default"` compares POINTERS, and
 * passes only when the compiler merges identical literals. This is a static table of literals that owns nothing, so it cannot be a
 * `RolltuiStr`: wrap in a view before comparing, or use `rolltui_embedded_text()` below, which compares correctly once. */
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
 * presets — the preset stores and domains
 * ======================================================================================== */

/* THE GENERIC ENGINE BEHIND ALL THREE — `RolltuiPresetDomain`, `RolltuiPresetStore` and the
 * `rolltui_preset_store_*`/`rolltui_preset_domain_*` family that drove them as one `void*`
 * mechanism over a function-pointer descriptor — moved to `c/rolltui_presets.h` once nothing
 * public still needed the generic shape: every real caller (dirktui, paint, the studio) always
 * knew which of the three domains it meant, so `RolltuiThemeStore`/`RolltuiLayoutStore`/
 * `RolltuiBindingsStore` (each beside its own domain, earlier in this book) are what a host is
 * offered, each backed by the same engine under a concrete name. The engine is unchanged and
 * still internal to the library — only the public SHAPE of it is gone. */

/* ---- ONE PRESET IN A LISTING, and the shape every "N things out" uses ---------------------
 * A result the library already has goes into a buffer the CALLER owns and reuses (`RolltuiStr*` for text, a growing list like this
 * for many things), REPLACED on every call. A callback is for a DECISION the library cannot make (`RolltuiScopeFn`, `RolltuiRowsFn`,
 * `RolltuiEffectFn`), never for handing back an answer: does it carry a decision IN, or a result OUT? Order: the shipped ones first,
 * "default" ahead of the rest (the order a chooser offers), then the user's "*.json" that do not shadow a shipped name. `path` is
 * empty for a shipped preset. */
typedef struct RolltuiPresetInfo {
  RolltuiStr name;
  RolltuiStr path; /* "" for a shipped preset */
  int shipped ROLLTUI_DEFAULT(0);
} RolltuiPresetInfo;

/* A caller-owned, reusable list of them. Zero-initialise; `_release` frees and zeroes it (a no-op on a zeroed list and on NULL); the
 * C++ destructor does that.
 * RolltuiPresetListRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiPresetList : public
 * RolltuiPresetListRaw` reclaiming the name, with the destructor and accessors. */
typedef struct RolltuiPresetListRaw {
  RolltuiPresetInfo* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);
} RolltuiPresetListRaw;

#define ROLLTUI_SAVE_SAVED 0

#define ROLLTUI_SAVE_REFUSED_SHIPPED 1

#define ROLLTUI_SAVE_EXISTS_ASK 2

#define ROLLTUI_SAVE_BAD_NAME 3

#define ROLLTUI_SAVE_WRITE_FAILED 4

/* The Theme-relevant report fields, transparent like `RolltuiThemeReport`: only `rolltui_presets.c` writes one; a caller reads it after
 * a parse call, then releases it. */
typedef struct RolltuiThemePresetReport {
  RolltuiStr error; /* non-empty: unusable */
  RolltuiStr* bad_values;   size_t bad_values_n,   bad_values_cap;   /* GROWING AMORTISED */
  RolltuiStr* unknown_keys; size_t unknown_keys_n, unknown_keys_cap; /* GROWING AMORTISED */
  RolltuiStr* notes;        size_t notes_n,        notes_cap;        /* GROWING AMORTISED */
  RolltuiThemeReport colours; /* the "colours" part's own report, verbatim */
} RolltuiThemePresetReport;

/* ---- the Theme domain ----------------------------------------------------------------------
 * `RolltuiThemePresetValue` is this domain's value type, and `rolltui_theme_preset_domain_init` is the only Theme domain there is
 * (`studio.cpp` and roll's frontend both build their store from it). */
typedef struct RolltuiThemePresetValue {
  RolltuiJsonValue* colours ROLLTUI_DEFAULT(nullptr); /* OWNED */
  RolltuiStr mode;                                     /* "auto" | "dark" | "light" */
  RolltuiStr depth;                                    /* "auto" | "truecolor" | "256" | "16" | "mono" */
} RolltuiThemePresetValue;

/* ---- the Layout domain ----------------------------------------------------------------------
 * The Value is `RolltuiLayout` itself (rolltui_layout.h). The shipped presets ARE the built-ins, embedded once and read by both a
 * host's built-in lookup and this domain, so the two cannot disagree. */
typedef struct RolltuiLayoutPresetReport {
  RolltuiStr error; /* the PRESET-level error: a copy of `layout.error` on failure, or the
                     * mechanics' own ("no layout preset 'x' ...") */
  RolltuiLayoutReport layout; /* the file's own: error, unknown_keys, bad_values, notes */
  RolltuiStr* notes;          /* one per `layout.notes` entry, plus whatever the mechanics
                               * itself adds */
  size_t notes_n, notes_cap;
} RolltuiLayoutPresetReport;

/* ---- the Bindings domain --------------------------------------------------------------------
 * The Value is `RolltuiBindings*` itself, so `clone` / `destroy` / `equal` are `rolltui_bindings_clone` / `_free` / `_equal`. */
typedef struct RolltuiBindingsPresetReport {
  RolltuiStr error; /* the PRESET-level error: a copy of `bindings.error` on failure, or the
                     * mechanics' own */
  RolltuiBindingsReport bindings; /* the file's own: unknown_actions/bad_chords/undeliverable/
                                   * conflicts/bad_values/unknown_keys (its "bindings" object) */
  RolltuiStr* unknown_keys; /* the PRESET file's own top-level keys other than "name" / "bindings" / "preset" (the bindings loader only looks at its "bindings" object) */
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* notes; /* whatever the mechanics itself adds */
  size_t notes_n, notes_cap;
} RolltuiBindingsPresetReport;

/* Reads a whole file; 0 when it cannot be opened. `put`/`ctx` because the library streams into its internal `Buf`; a consumer that wants
 * the bytes passes `rolltui_str_put` and a `RolltuiStr*`. */
int rolltui_preset_read_file(const char* path, size_t path_len, RolltuiPutFn put, void* ctx);

/* Writes to a sibling temp file, then renames: a reader sees the old complete file or the new complete file, never a mix. On failure, 0,
 * and the reason REPLACES `*err` (which may be NULL). */
int rolltui_preset_write_file_atomic(const char* path, size_t path_len, const char* bytes, size_t len,
                                     RolltuiStr* err);

/* The SENTENCE for an outcome: a table beside the constants it is indexed by. BORROWS a static literal; `*len` may be NULL; an
 * out-of-range code reads back as "". WRITE_FAILED's own reason is still the caller's, through the concrete stores' own `_save_as`/
 * `_add`, which write it into their own `err` for every result but SAVED. */
const char* rolltui_preset_save_result_text(int result, size_t* len);

void rolltui_preset_list_release(RolltuiPresetListRaw* l);

/* Frees everything and zeroes the struct: safe on an already-zeroed one and on repeated calls (the "reset, not just release" contract of
 * every report here). `_clean`/`_summary` below (same trio for theme and bindings, each beside its own domain): `_clean` is a
 * JUDGEMENT about the library's own data, so it is the library's; `_summary` APPENDS the sentence `rolltui_preset_report_summary`
 * would, with that domain's fields already wired. */
void rolltui_layout_preset_report_release(RolltuiLayoutPresetReport* r);

int rolltui_layout_preset_report_clean(const RolltuiLayoutPresetReport* r);

void rolltui_layout_preset_report_summary(const RolltuiLayoutPresetReport* r, RolltuiStr* out);

/* ========================================================================================
 * settings handle — the keys a person changes, over the three working copies
 * Which working copy a key lives in is the library's business: a host holds one `RolltuiSettings` over its three stores and spells
 * keys, and the routing, the precedence and the sentence a bad value is refused with are all behind the handle.
 * ======================================================================================== */

/* Where a resolved value came from. THREE rungs: a fourth (a flag) would be a second configuration system with neither discoverability
 * nor persistence. */
typedef enum RolltuiSettingRung {
  ROLLTUI_SETTING_RUNG_ENV = 0,
  ROLLTUI_SETTING_RUNG_WORKING,
  ROLLTUI_SETTING_RUNG_BUILTIN,
} RolltuiSettingRung;

/* One key. BORROWED fields throughout — every string is a literal alive for the process's
 * whole life. */
typedef struct RolltuiSetting {
  const char* key;
  size_t key_len;
  /* The tail of the environment variable that fills this key, joined to a host's own prefix:
   * "THEME" is roll's ROLL_THEME. */
  const char* env_suffix;
  size_t env_suffix_len;
  const char* builtin;
  size_t builtin_len;
  /* The legal values, e.g. "auto | dark | light" — both the help a listing prints and the
   * sentence a rejected value is named against, so there is one spelling of the set. */
  const char* values;
  size_t values_len;
  /* The working copy this key lives in, by the name its preset files, its store and its own
   * identity setting all already carry: "theme" | "layout" | "bindings". */
  const char* store;
  size_t store_len;
  /* Non-zero when this key's value IS its store's preset name, so the value and the store's label describe one thing and saying both says
   * it twice. Zero for a key that is one field inside a preset. */
  unsigned char names_preset;
} RolltuiSetting;

/* What a change had to say, in the words a host would print; every field is empty after a clean success. It is not a fourth domain
 * report: it holds no domain's fields and carries no tag. */
typedef struct RolltuiSettingsReport {
  RolltuiStr error;     /* why the change did not happen */
  RolltuiStr problems;  /* the value took effect, and the file it came from said this */
  RolltuiStrListRaw notes; /* what the load kept, rewrote or ignored — notes are not problems */
} RolltuiSettingsReport;

/* The three stores are BORROWED and must outlive the handle. Any of the three may be NULL — a
 * host that never opened one just has no working copy for that domain's keys, which is how the
 * built-in rung is reached at all for it. */
RolltuiSettings* rolltui_settings_new(RolltuiThemeStore* theme, RolltuiLayoutStore* layout,
                                      RolltuiBindingsStore* bindings);

void rolltui_settings_free(RolltuiSettings* s);

size_t rolltui_settings_count(void);

/* BORROW, table order ("theme", "layout", "theme_mode", "color_depth", "bindings" — the order
 * a listing offers them in), valid for the process's whole life. NULL past the end. */
const RolltuiSetting* rolltui_settings_at(size_t i);

/* The row named `key`, or NULL when `key` is not a setting. */
const RolltuiSetting* rolltui_settings_find(const char* key, size_t key_len);

/* The value `key` currently has, read from whichever working copy owns it. REPLACES `*out`,
 * and empties it for a key that is not a setting. */
void rolltui_settings_get(const RolltuiSettings* s, const char* key, size_t key_len, RolltuiStr* out);

/* The label of the working copy `key` lives in: "mono", or "mono (modified)". REPLACES `*out`. For an identity key (`names_preset`) it
 * describes what `_get` returns; for a field key it names the preset the field is edited inside. */
void rolltui_settings_label(const RolltuiSettings* s, const char* key, size_t key_len, RolltuiStr* out);

/* THE WHOLE RULE: the environment, then the working copy, then the built-in default; the first non-empty one wins. An empty `env` means
 * "not given there". REPLACES `*out_value` and sets `*out_rung` (may be NULL). */
void rolltui_settings_resolve(const RolltuiSettings* s, const char* key, size_t key_len, const char* env,
                              size_t env_len, RolltuiStr* out_value, RolltuiSettingRung* out_rung);

/* A BORROW of a static string literal, never freed: "environment" | "working copy" |
 * "built-in default". */
const char* rolltui_setting_rung_name(RolltuiSettingRung r, size_t* len);

/* Applies `value` to whichever working copy owns `key`: a preset LOAD for an identity key, a checked field write otherwise. Returns 1
 * when the value took effect. `persist` zero fills this run's copy without writing it (what an environment value gets). `report` may be
 * NULL; when not, it is REPLACED and the caller releases it with `rolltui_settings_report_release`. */
int rolltui_settings_set(RolltuiSettings* s, const char* key, size_t key_len, const char* value,
                         size_t value_len, int persist, RolltuiSettingsReport* report);

void rolltui_settings_report_release(RolltuiSettingsReport* r); /* frees everything; zeroes */

/* ========================================================================================
 * BOOK 3 — WIDGETS: what actually renders
 * The built-in widget kinds (input, menu, transcript, rows, the file picker, …) and the machinery that supports them: effects,
 * markdown, diff, the window registry and window stack a host reaches a kind's sources through, and the hint bar a status line
 * shows them with.
 * ======================================================================================== */

/* ========================================================================================
 * document — the transcript document a host appends to
 * ======================================================================================== */

#ifdef __cplusplus
namespace rolltui {
// Declared, not defined: an opaque enum declaration with a fixed underlying type is a complete type, which is all a member needs.
enum class Role : unsigned char;
enum class EffectState : unsigned char;

}  // namespace rolltui
#endif

/* RolltuiDocEntryRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiDocEntry : public
 * RolltuiDocEntryRaw` reclaiming the name, with the destructor, move, and `clone()`. */
typedef struct RolltuiDocEntryRaw {
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
} RolltuiDocEntryRaw;

/* The entries, OWNED and individually allocated so an append never moves one. Laid out as
 * `RolltuiPtrVec` by construction, so the append mechanics are that one's.
 * RolltuiDocumentRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiDocument : public
 * RolltuiDocumentRaw` reclaiming the name, with the destructor, move, iterators, `clone()`. */
typedef struct RolltuiDocumentRaw {
  RolltuiDocEntryRaw** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);
} RolltuiDocumentRaw;

void rolltui_doc_entry_release(RolltuiDocEntryRaw* e);

void rolltui_doc_entry_copy(RolltuiDocEntryRaw* to, const RolltuiDocEntryRaw* from);

/* Appends an EMPTY entry and returns it — the C's `emplace_back`. */
RolltuiDocEntryRaw* rolltui_document_add(RolltuiDocumentRaw* d);

void rolltui_document_clear(RolltuiDocumentRaw* d);   /* releases every entry, keeps the array */

/* Removes entry `i`, keeping the order of the rest: an inline host's finished entry, once committed. */
void rolltui_document_remove(RolltuiDocumentRaw* d, size_t i);

void rolltui_document_release(RolltuiDocumentRaw* d); /* …and the array */

void rolltui_document_copy(RolltuiDocumentRaw* to, const RolltuiDocumentRaw* from);

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
/* `rolltui::InputOptions` IS this struct. The two strings are OWNED `RolltuiStr`s; a copy is a named function.
 * RolltuiInputOptionsRaw: the plain data. `rolltui_cpp.h` declares a `RolltuiInputOptions` class
 * reclaiming the name, with the one special member (the default prompt). Its declared
 * `operator==` is dropped: no definition existed anywhere, and nothing called it. */
typedef struct RolltuiInputOptionsRaw {
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
  /* ONE ROW: a newline is dropped, the text never wraps, and the row slides under the caret
   * — a menu field, an address bar. Off: the text wraps and the window grows with it. */
  unsigned char single_line ROLLTUI_DEFAULT(0);
  /* ---- the editor ---------------------------------------------------------------------------
   * A row breaks after a space rather than at the area's edge, so a word is never split; a word longer than the row still is. */
  unsigned char wrap_words ROLLTUI_DEFAULT(0);
  /* Rows wrap at this many text columns, or at the area's edge when that is nearer: a commit message's 72. 0: the area's edge. */
  int wrap_column ROLLTUI_DEFAULT(0);
  /* A gutter after the prompt numbering each line, in `RolltuiInputRoles::line_number`; a wrapped line's later rows leave it blank. */
  unsigned char line_numbers ROLLTUI_DEFAULT(0);
  /* A newline starts the new line with the spaces and tabs the line it breaks begins with. */
  unsigned char auto_indent ROLLTUI_DEFAULT(0);
  /* A LANGUAGE the syntax engine knows (`"c"`, `"json"`, a name a fence would carry): the text is highlighted as that language's code,
   * by the library's own languages and the person's (`<config>/rolltui/syntax/`). Empty: plain text. */
  RolltuiStr language;
  /* WHAT IS SHOWN of a secret: ROLLTUI_INPUT_SHOW_ALL (0), _MASKED (one `•` a grapheme, `*` where that glyph is drawn two cells wide)
   * or _NOTHING (the caret stays after the prompt, as `sudo` asks). Anything but ALL is never copied, never kept in the history, and
   * moves by whole lines, so word motions cannot spell out where the spaces are. */
  unsigned char secret ROLLTUI_DEFAULT(0);
  /* Submit does nothing while the text is empty or only spaces, tabs and newlines. */
  unsigned char refuse_empty ROLLTUI_DEFAULT(0);
  /* Up and Down recall only the entries that begin with the text before the caret when the first Up was pressed: type `git` and Up
   * finds the last `git` line; with the caret at the start (or nothing typed), every entry. */
  unsigned char history_prefix ROLLTUI_DEFAULT(1);
  /* 0: a printable key is an action like any other, looked up in the bindings, and nothing is typed when none is bound — a modal
   * editor's normal mode, made from a bindings table and `rolltui_input_perform`. 1: a printable key types itself. */
  unsigned char insert_typed ROLLTUI_DEFAULT(1);
  /* THE CARET'S SHAPE, ROLLTUI_CARET_*; 0: by the mode, a bar while printable keys type and a block while they are actions. */
  unsigned char caret ROLLTUI_DEFAULT(0);
} RolltuiInputOptionsRaw;

#define ROLLTUI_INPUT_SHOW_ALL 0
#define ROLLTUI_INPUT_SHOW_MASKED 1
#define ROLLTUI_INPUT_SHOW_NOTHING 2

/* ---- the selection ------------------------------------------------------------------------ */
/* `rolltui::InputSelection` IS this struct. RolltuiInputSelectionRaw: the plain data.
 * `rolltui_cpp.h` declares `class RolltuiInputSelection : public RolltuiInputSelectionRaw`
 * reclaiming the name, with begin()/end()/empty(). */
typedef struct RolltuiInputSelectionRaw {
  size_t anchor ROLLTUI_DEFAULT(0), head ROLLTUI_DEFAULT(0);
  unsigned char active ROLLTUI_DEFAULT(0);
} RolltuiInputSelectionRaw;

/* THE HOST'S CLIPBOARD: a function pointer and a context. Set once; NULL turns it off. The text is a BORROW for the call. */
typedef void (*RolltuiCopyFn)(void* ctx, const char* text, size_t len);

/* The thirty-seven action names, in command order. The input knows what each command DOES and none of the words. */

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
  const char* yank;            /* the text the last run of kills took, at the caret */
  const char* transpose_chars; /* the graphemes either side of the caret change places (the last two at a line's end) */
  const char* transpose_words; /* the word at or before the caret and the one after it change places */
  const char* upcase_word;     /* the caret to the end of its word uppercased, or the selection */
  const char* downcase_word;
  const char* capitalize_word; /* each word's first letter titled and the rest lowered */
  const char* overwrite;       /* typing replaces the grapheme at the caret, until pressed again */
  const char* normal_mode;     /* from typing to vim's normal mode (`rolltui_input_mode`): bound by the vim preset, not the default */
  const char* complete;          /* the field's completer: the word completed, or the next candidate in the list (Tab) */
  const char* complete_prev;     /* the previous candidate in the list (Shift-Tab) */
  const char* accept_suggestion; /* the suggestion after the caret taken (Ctrl-F; Right at the end of the text too) */
} RolltuiInputActions;

/* THE FOUR ROLES A DRAW NEEDS, handed in as bytes. `prompt` is the OPTIONS' role, which is
 * why it is not in here. */

typedef struct RolltuiInputRoles {
  unsigned char text;
  unsigned char selection;
  unsigned char placeholder;
  unsigned char line_number; /* the gutter's numbers (`line_numbers`) */
} RolltuiInputRoles;


/* ---- input ---------------------------------------------------------------------------------*/

/* The defaults, for a C caller — `= {0}` would give a zero tab width and no prompt role,
 * which is the language answering a question nobody asked. */
void rolltui_input_options_release(RolltuiInputOptionsRaw* o);

void rolltui_input_options_copy(RolltuiInputOptionsRaw* to, const RolltuiInputOptionsRaw* from);

int rolltui_input_options_equal(const RolltuiInputOptionsRaw* a, const RolltuiInputOptionsRaw* b);

void rolltui_input_set_copy(RolltuiInput* in, RolltuiCopyFn fn, void* ctx);

/* ---- content -------------------------------------------------------------------------------- */
/* A BORROW, valid until the text next changes. */
const char* rolltui_input_text(const RolltuiInput* in, size_t* len);

void rolltui_input_clear(RolltuiInput* in);
/* A host PREFILLS: a path line showing the folder the cursor is in, a name to correct. The text is copied, the caret goes to its end, the
 * selection is dropped. `select_all` is an address bar's rule on reaching it: typing replaces the whole, a first arrow key places the caret. */
void rolltui_input_set_text(RolltuiInput* in, const char* text, size_t len);
void rolltui_input_select_all(RolltuiInput* in);

/* The LIBRARY'S OWN thirty-seven, so a consumer need not spell them to call `handle`. BORROWS static storage; a host with different words
 * still passes its own struct. */
const RolltuiInputActions* rolltui_input_default_actions(void);

/* THE CARET, as a byte offset into the text, snapped to a grapheme boundary. `extend` 1 moves the selection's head and keeps its
 * anchor, so two calls select a range: a rename puts the caret before a file's extension, or selects the name without it. */
size_t rolltui_input_caret(const RolltuiInput* in);
void rolltui_input_set_caret(RolltuiInput* in, size_t byte, int extend);

/* The text as it is WRAPPED at `column` text columns (0: as last drawn), each soft break a newline and the spaces before it dropped. A
 * commit message typed into a 72-column editor is read back with the breaks it was seen with. REPLACES `*out`. */
void rolltui_input_wrapped_text(RolltuiInput* in, int column, RolltuiStr* out);

/* RUNS ONE COMMAND BY ITS ACTION NAME, as a bound key would: one of the `RolltuiInputActions` names in `actions`. Returns what
 * `rolltui_input_handle` would (ROLLTUI_INPUT_IGNORED for a name that is not one of them). */
int rolltui_input_perform(RolltuiInput* in, const RolltuiInputActions* actions, const char* action, size_t len,
                          unsigned long long now_ms);
/* VIM'S MODES, where the keys preset binds `input.normal_mode` (the vim preset binds it to Escape): that key leaves typing for NORMAL
 * mode, where a printable key types nothing and is a command of the `normal` scope (motions, operators, `i`, `a`, `v`, `r`, `R`, `p`,
 * `u`), and Escape is no longer the field's, so the popup the field is in closes on a second one. VISUAL selects from where it began to
 * the caret, both characters included, for the `visual` scope's operators; REPLACE types over what is there. The caret is a block in
 * normal and visual mode, an underline in replace mode and a bar while typing; it sits on a character outside typing, as vim's does. */
#define ROLLTUI_INPUT_INSERT 0
#define ROLLTUI_INPUT_NORMAL 1
#define ROLLTUI_INPUT_VISUAL 2
#define ROLLTUI_INPUT_REPLACE 3
int rolltui_input_mode(const RolltuiInput* in);
/* ONE `normal.*` OR `visual.*` COMMAND BY NAME, as its bound key runs it in that mode (what a key sequence such as `d w` resolves to);
 * ROLLTUI_INPUT_IGNORED outside its mode or for a name that is not one. */
unsigned char rolltui_input_perform_modal(RolltuiInput* in, const char* action, size_t len);

/* ---- history ---------------------------------------------------------------------------------- */
/* What Submit keeps: the newest last, a run of the same entry kept once, at most `history_limit` (the oldest go). A secret is never
 * kept. Up and Down browse it (`history_prefix`). */
void rolltui_input_push_history(RolltuiInput* in, const char* entry, size_t len);
size_t rolltui_input_history_count(const RolltuiInput* in);
/* A BORROW, valid until the history next changes; "" past the end. */
const char* rolltui_input_history_at(const RolltuiInput* in, size_t i, size_t* len);
void rolltui_input_history_clear(RolltuiInput* in);
/* THE HISTORY IN A FILE, kept across runs: a JSON array of strings, the newest last. `load` REPLACES the history with the file's (a
 * missing file is an empty history, not an error). `save` reads the file again first and writes its entries followed by the ones this
 * input gained since it last loaded or saved, capped at `history_limit`, through a temporary file and a rename — so two instances
 * saving in turn keep both their entries, and a reader never sees half a file. 0 on failure with the reason in `*err` (may be NULL). */
int rolltui_input_history_load(RolltuiInput* in, const char* path, size_t path_len, RolltuiStr* err);
int rolltui_input_history_save(RolltuiInput* in, const char* path, size_t path_len, RolltuiStr* err);

/* ---- layout and drawing -------------------------------------------------------------------------- */
void rolltui_input_set_options(RolltuiInput* in, const RolltuiInputOptionsRaw* o);
/* COMPLETION. `fn` is asked for the candidates whenever the text or the caret moved: it adds them, best first, with
 * `rolltui_completions_add` (a candidate and an optional detail shown beside it in the list), and says which bytes of the text they
 * replace with `rolltui_completions_replace` (the word before the caret unless it says). With a completer, Tab (`input.complete`)
 * takes the only candidate, or what every candidate begins with past the word, or else opens a list under the field (above it where
 * there is no room) and puts each candidate in the word's place in turn; Shift-Tab, Up and Down step through it, Enter keeps the one
 * shown and Escape puts the word back. The rest of the first candidate the word begins is drawn faded after the caret, and Ctrl-F
 * (`input.accept_suggestion`) or Right at the end of the text takes it. Without one, the field does not take Tab. NULL `fn`: none. */
typedef struct RolltuiCompletions RolltuiCompletions;
typedef void (*RolltuiCompleteFn)(void* ctx, const char* text, size_t len, size_t caret, RolltuiCompletions* out);
void rolltui_input_set_completer(RolltuiInput* in, RolltuiCompleteFn fn, void* ctx);
void rolltui_completions_add(RolltuiCompletions* out, const char* text, size_t len, const char* detail, size_t detail_len);
void rolltui_completions_replace(RolltuiCompletions* out, size_t begin, size_t end);
/* Ready-made candidates for a completer: `words` that fuzzily match `word` (those it begins first), and the names in a folder
 * matching the last part of a path (`dir` the folder a relative path starts from, the working directory when NULL; `~/` is home; a
 * folder ends in `/`; hidden names only when the word starts with a dot). */
void rolltui_complete_words(RolltuiCompletions* out, const char* word, size_t len, const char* const* words, size_t n);
void rolltui_complete_path(RolltuiCompletions* out, const char* word, size_t len, const char* dir, size_t dir_len);

const RolltuiInputOptionsRaw* rolltui_input_options(const RolltuiInput* in);

/* Shared by the input plugin's own `handle` slot and by a host asking for the action back: handle `e` against the live bindings, and on
 * Submit either clear-and-push-history or keep the text (`rolltui_windows_on_submit` says which), then call whatever is bound to
 * `source`. Returns one of ROLLTUI_INPUT_IGNORED / HANDLED / SUBMIT / EOF. */
int rolltui_input_kind_process_event(RolltuiInput* ed, RolltuiWindows* w, const char* source, size_t source_len,
                                      const RolltuiEvent* e);

/* ========================================================================================
 * markdown — the markdown parser over md4c; the transcript renders through md_lines, and no host calls it
 * ======================================================================================== */

/* One verbatim line of a code block, as a borrow. */
typedef struct RolltuiMdCodeLine {
  const char* p;
  size_t n;
} RolltuiMdCodeLine;

/* Where a highlighter puts one span; supplied by the renderer, valid for the call only. `role` is a Role; its two high bits are free:
 * ROLLTUI_MD_ROLE_BOLD and ROLLTUI_MD_ROLE_ITALIC draw the span in that role AND bold or italic on top of the role's own style. A byte
 * without them draws as it always did. */
#define ROLLTUI_MD_ROLE_MASK 0x3Fu
#define ROLLTUI_MD_ROLE_BOLD 0x40u
#define ROLLTUI_MD_ROLE_ITALIC 0x80u
typedef void (*RolltuiMdSpanSink)(void* sink, size_t begin, size_t end, unsigned char role);

/* THE SYNTAX-HIGHLIGHTING SEAM. Emits the spans of `lines[index]` through `sink`, in any order and number. It emits DATA, never a
 * painter: a span that overlaps a prior one, runs backwards or exceeds the line is clamped and NAMED in the store's report. Called
 * once per code line of every Code block, never for an HTML block. */
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

/* What an Input item accepts. `rolltui::InputSpec` IS this struct.
 * RolltuiInputSpecRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiInputSpec : public
 * RolltuiInputSpecRaw` reclaiming the name, with `clone()`; `operator==` is free. */
typedef struct RolltuiInputSpecRaw {
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
} RolltuiInputSpecRaw;

struct RolltuiMenuItem;

/* PINNED PUBLIC BY A PUBLIC STRUCT'S C++ MEMBERS: `RolltuiMenuItem` holds a `RolltuiInputSpecRaw` by value, and its `clone()` and
 * `operator==` call these. */
void rolltui_input_spec_copy(RolltuiInputSpecRaw* to, const RolltuiInputSpecRaw* from);
int rolltui_input_spec_equal(const RolltuiInputSpecRaw* a, const RolltuiInputSpecRaw* b);

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
  RolltuiMenuItemList(const RolltuiMenuItemList&) = delete;  /* `rolltui_menu_list_copy` */
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

/* A SECTION is a heading over the rows below it (its label, a rule to the edge, a blank row before it unless it is the level's first);
 * a SEPARATOR is the rule alone. They organise a level and nothing else: neither takes the cursor or a click, matches a typed filter, or
 * appears in a palette. File keys: `"kind": "section"` with a `label`, `"kind": "separator"`. */
#define ROLLTUI_MENU_SECTION 5
#define ROLLTUI_MENU_SEPARATOR 6

/* A LEVEL THAT IS A TABLE: its rows' label is column 0, which takes whatever width the others leave, and after it up to
 * ROLLTUI_MENU_COLUMNS columns of a fixed width, each a row's `cells` in turn, right-aligned unless the column says left. A section's
 * cells are the table's head, aligned as its rows are. Narrower than the columns need, the label keeps its last eight cells and the
 * columns go from the right, never wrapping. File keys: a level's `columns`, a list of `{width, align}` (`width` a whole number of
 * cells, `align` left or right, right where it says none), and an item's `cells`, a list of strings. */
#define ROLLTUI_MENU_COLUMNS 8
#define ROLLTUI_MENU_ALIGN_RIGHT 0
#define ROLLTUI_MENU_ALIGN_LEFT 1

typedef struct RolltuiMenuColumn {
  unsigned short width ROLLTUI_DEFAULT(0); /* in cells, 1 to 1000 */
  unsigned char align ROLLTUI_DEFAULT(ROLLTUI_MENU_ALIGN_RIGHT);
} RolltuiMenuColumn;

/* A TABLE IS STORED ONLY WHERE THERE IS ONE: a row's cells and a level's columns are two lists that allocate nothing while empty, so
 * a menu that draws no table pays two empty lists per item and nothing more. READ them freely; WRITE them only through
 * `rolltui_menu_item_add_cell` / `rolltui_menu_item_set_columns`, which hold what a table may be. Each frees itself in C++ and moves;
 * in C, `rolltui_menu_item_release` frees both, as it frees the rest of an item. */
typedef struct RolltuiMenuCells {
  RolltuiStr* v ROLLTUI_DEFAULT(nullptr); /* OWNED */
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  RolltuiMenuCells() = default;
  RolltuiMenuCells(const RolltuiMenuCells&) = delete; /* `rolltui_menu_item_copy` copies a whole item */
  RolltuiMenuCells(RolltuiMenuCells&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiMenuCells& operator=(const RolltuiMenuCells&) = delete;
  RolltuiMenuCells& operator=(RolltuiMenuCells&& o) noexcept;
  ~RolltuiMenuCells();
#endif
} RolltuiMenuCells;

typedef struct RolltuiMenuColumns {
  RolltuiMenuColumn* v ROLLTUI_DEFAULT(nullptr); /* OWNED, exactly `n` long */
  size_t n ROLLTUI_DEFAULT(0);
#ifdef __cplusplus
  RolltuiMenuColumns() = default;
  RolltuiMenuColumns(const RolltuiMenuColumns&) = delete;
  RolltuiMenuColumns(RolltuiMenuColumns&& o) noexcept : v(o.v), n(o.n) {
    o.v = nullptr;
    o.n = 0;
  }
  RolltuiMenuColumns& operator=(const RolltuiMenuColumns&) = delete;
  RolltuiMenuColumns& operator=(RolltuiMenuColumns&& o) noexcept;
  ~RolltuiMenuColumns();
#endif
} RolltuiMenuColumns;

typedef struct RolltuiMenuItem {
#ifdef __cplusplus
  enum class Kind : unsigned char { Action = 0, Submenu, Toggle, Choice, Input, Section, Separator };
  Kind kind = Kind::Action;
#else
  unsigned char kind;
#endif
  RolltuiStr id;          /* the action id the host binds; an option's value id */
  RolltuiStr label;
  RolltuiStr action_name; /* the BINDINGS action this item does the same thing as */
  RolltuiStr shortcut;    /* display only; with `action_name` set it is the live chords */
  RolltuiStr description; /* shown by a host that wants one (a help line, a tooltip); the library draws nothing with it */
  unsigned char enabled ROLLTUI_DEFAULT(1);
  unsigned char checked ROLLTUI_DEFAULT(0); /* Toggle */
  /* Choice: its options open as a DROPDOWN, a small box over the menu, rather than as a level of their own. A level suits a set that
   * IS a place (a submenu); a dropdown suits an ANSWER (sort by name / size / modified), where leaving the screen loses the context.
   * Choosing sets the answer and keeps the box open; Escape, Left, or choosing the standing answer closes it. A disabled option is listed,
   * muted, never landed on or chosen; a choice whose every option is disabled is disabled. File key: `"dropdown": true`. */
  unsigned char dropdown ROLLTUI_DEFAULT(0);
  /* Submenu: entering it floats it as its own box over the level it was entered from (the treatment `dropdown` gives a Choice) instead
   * of replacing the view. Right for a level where you ACT (the chords bound to one action) and want the context kept in view; wrong for
   * a level you navigate through. Nests ONE level: a popup entered from a popup replaces the view like a plain one. File key: `"popup": true`. */
  unsigned char popup ROLLTUI_DEFAULT(0);
  /* Choice or Input: what it holds is a COLOUR, so it is shown as one (`rolltui_frame_put_swatch`'s two cells beside the spelling): a
   * Choice's value and each option, an Input's text as typed, hollow until it parses. An Input of type `color` needs no flag.
   * File key: `"swatch": true`. */
  unsigned char swatch ROLLTUI_DEFAULT(0);
  /* WHAT IT DOES IS RUNNING: a spinner after the label, from `busy_since_ms`, until the host clears it. Set at run time, never by a
   * file: a host that started the action marks the row it came from. */
  unsigned char busy ROLLTUI_DEFAULT(0);
  unsigned long long busy_since_ms ROLLTUI_DEFAULT(0);
  RolltuiStr value;       /* Choice: the current option id; Input: the COMMITTED text */
  RolltuiInputSpecRaw spec;  /* Input: the type and its constraints */
  RolltuiMenuItemList children; /* Submenu: items; Choice: options */
  RolltuiMenuCells cells;     /* a row of a table: what it says in each of its level's columns after its label */
  RolltuiMenuColumns columns; /* a level that is a table (the root, or a Submenu): its columns after the label */

#ifdef __cplusplus
  RolltuiMenuItem();
  bool operator==(const RolltuiMenuItem& o) const;
  RolltuiMenuItem clone() const;
  // The builders every host writes items with: C strings in, `rolltui_menu_item_set` underneath.
  static RolltuiMenuItem action(const char* id, const char* label, const char* shortcut = "");
  static RolltuiMenuItem submenu(const char* id, const char* label);
  static RolltuiMenuItem toggle(const char* id, const char* label, bool checked);
  static RolltuiMenuItem choice(const char* id, const char* label, const char* value);  /* options: children.push_back */
  static RolltuiMenuItem input(const char* id, const char* label, const char* value = "");
  static RolltuiMenuItem input(const char* id, const char* label, RolltuiInputSpecRaw spec, const char* value = "");
  static RolltuiMenuItem section(const char* id, const char* label);
  static RolltuiMenuItem separator(const char* id);
#endif
} RolltuiMenuItem;

/* ---- forward declarations the C++ members just below call ----------------------------------*/
void rolltui_menu_item_copy(RolltuiMenuItem* to, const RolltuiMenuItem* from);
int rolltui_menu_item_equal(const RolltuiMenuItem* a, const RolltuiMenuItem* b);
void rolltui_menu_item_set(RolltuiMenuItem* it, unsigned char kind, const char* id, size_t id_len, const char* label,
                           size_t label_len, const char* shortcut, size_t shortcut_len);
RolltuiMenuItem* rolltui_menu_list_add(RolltuiMenuItemList* l);
void rolltui_menu_list_clear(RolltuiMenuItemList* l);
void rolltui_menu_list_release(RolltuiMenuItemList* l);
/* Frees what a row's cells or a level's columns hold and empties them: a row's cells refill from here. */
void rolltui_menu_cells_release(RolltuiMenuCells* c);
void rolltui_menu_columns_release(RolltuiMenuColumns* c);

#ifdef __cplusplus
/* ---- the C++ special members of the structs above -----------------------------------------
 * Each calls a C function declared above, so "release this subtree" has one implementation; `inline`, beside the declarations,
 * keeps the library free of any C++ translation unit. */
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

inline RolltuiMenuCells::~RolltuiMenuCells() { rolltui_menu_cells_release(this); }
inline RolltuiMenuCells& RolltuiMenuCells::operator=(RolltuiMenuCells&& o) noexcept {
  if (this != &o) {
    rolltui_menu_cells_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}
inline RolltuiMenuColumns::~RolltuiMenuColumns() { rolltui_menu_columns_release(this); }
inline RolltuiMenuColumns& RolltuiMenuColumns::operator=(RolltuiMenuColumns&& o) noexcept {
  if (this != &o) {
    rolltui_menu_columns_release(this);
    v = o.v;
    n = o.n;
    o.v = nullptr;
    o.n = 0;
  }
  return *this;
}

inline RolltuiMenuItem::RolltuiMenuItem() = default;

inline bool RolltuiMenuItem::operator==(const RolltuiMenuItem& o) const {
  return rolltui_menu_item_equal(this, &o) != 0;
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
inline RolltuiMenuItem RolltuiMenuItem::input(const char* id, const char* label, RolltuiInputSpecRaw spec, const char* value) {
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
inline RolltuiMenuItem RolltuiMenuItem::section(const char* id, const char* label) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Section), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::separator(const char* id) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Separator), id, std::strlen(id), "", 0, nullptr, 0);
  return it;
}

#endif

#define ROLLTUI_MENU_EVENT_NONE 0

#define ROLLTUI_MENU_EVENT_ACTIVATE 1

#define ROLLTUI_MENU_EVENT_TOGGLE 2

#define ROLLTUI_MENU_EVENT_CHOOSE 3

#define ROLLTUI_MENU_EVENT_INPUT 4

#define ROLLTUI_MENU_EVENT_CLOSED 5

/* The focus moved to a different item — an arrow key, a typed filter narrowing the match, a dropdown's own up/down — with nothing
 * else to report: `id` is the newly-focused item's, `value`/`checked` are untouched. A click moves the focus and acts, so it reports
 * what it did instead. Never fires while editing a typed field (that is not browsing), and never alongside another kind:
 * ACTIVATE/TOGGLE/CHOOSE/INPUT/CLOSED always win. */
#define ROLLTUI_MENU_EVENT_MOVED 6

typedef struct RolltuiMenuEvent {
  unsigned char kind ROLLTUI_DEFAULT(ROLLTUI_MENU_EVENT_NONE);
  RolltuiStr id;    /* the acted-on item (Choose: the Choice item) */
  RolltuiStr value; /* Choose: the option id; Input: the committed (canonical) text */
  unsigned char checked ROLLTUI_DEFAULT(0); /* Toggle: the new state */
} RolltuiMenuEvent;

/* `editor` is BORROWED and must outlive the menu — `rolltui::Menu` owns it. */

/* ---- the tree ---------------------------------------------------------------------------------- */

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
  /* …and the INPUT widget's own actions, because a typed field forwards every key it does not claim to the editor. */
  const RolltuiInputActions* input;
} RolltuiMenuActions;

/* THE HOST'S TEXT VALIDATORS, asked rather than moved. Returns 1 when a validator by that
 * name is registered; `why` is filled (non-empty) when it REFUSES the text. */
typedef int (*RolltuiValidatorFn)(void* ctx, const char* name, size_t nlen, const char* text, size_t tlen,
                                  RolltuiStr* why);

/* One (item id -> action name) pair, and a caller-owned list of them (a result the library already has goes into the caller's buffer,
 * not through a sink). Zero-initialise; the C++ destructor releases it. */
typedef struct RolltuiMenuAction {
  RolltuiStr id;
  RolltuiStr action;
} RolltuiMenuAction;

/* RolltuiMenuActionListRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiMenuActionList :
 * public RolltuiMenuActionListRaw` reclaiming the name, with the destructor and accessors. */
typedef struct RolltuiMenuActionListRaw {
  RolltuiMenuAction* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);
} RolltuiMenuActionListRaw;

typedef struct RolltuiMenuOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int inset ROLLTUI_DEFAULT(0); /* columns kept clear on each side of the area */
  unsigned char cursor_shape ROLLTUI_DEFAULT(0); /* ROLLTUI_CURSOR_*: CAPS and EDGES are drawn in the inset's cells */
  int scrolloff ROLLTUI_DEFAULT(0);  /* rows kept between the selection and the edge; past half the view, centred */
  unsigned char wrap ROLLTUI_DEFAULT(0); /* the selection goes from the last row to the first and back */
} RolltuiMenuOptions;

/* THE ROLES A DRAW NEEDS, handed in as bytes. Plus the input widget's three, which the
 * editor's own draw takes. */
typedef struct RolltuiMenuRoles {
  unsigned char item;
  unsigned char selected;
  unsigned char breadcrumb;
  unsigned char shortcut;
  unsigned char text_muted;
  unsigned char warning;
  unsigned char scroll_marker;
  /* A row that carries a value is two things: `Ink: #d8dce2` and `Shading  ascii` are a name and an answer, so they draw in two styles.
   * Only the FOREGROUND is taken: the row keeps its own background, so a selected row stays one solid block. */
  unsigned char label;
  unsigned char value;
  unsigned char hover; /* the row under the pointer (`RolltuiResolvedNode::hover`) */
  /* The selected row while the menu's window does not have focus, as a list's cursor dims: the person sees where they were, and that
   * keys go elsewhere. 0 draws it as `selected`. */
  unsigned char selected_away;
} RolltuiMenuRoles;

/* The report, transparent like `RolltuiBindingsReport` and `RolltuiLayoutReport`: `RolltuiStr` values in growing amortised arrays,
 * one per load-report field. Zero-initialise before use. */
typedef struct RolltuiMenuLoadReport {
  RolltuiStr error; /* non-empty: unusable, and rolltui_menu_parse_json returns 0 */
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
} RolltuiMenuLoadReport;

/* Serialises `root`, 2-space indented with a trailing newline. REPLACES `*out`. */
void rolltui_menu_item_init(RolltuiMenuItem* it);

/* Frees everything below and inside `it` and leaves it clean: the pair to `_init`, and to the parser that FILLS a caller-owned item.
 * Without it a C consumer can build one and cannot free it. */
void rolltui_menu_item_release(RolltuiMenuItem* it);

/* A ROW'S NEXT CELL, after those it has: 1, or 0 with nothing added once the row has ROLLTUI_MENU_COLUMNS. To give a row new cells,
 * `rolltui_menu_cells_release(&it->cells)` first. */
int rolltui_menu_item_add_cell(RolltuiMenuItem* it, const char* text, size_t len);

/* A LEVEL'S COLUMNS, REPLACED by a copy of `columns[0..n)`; `n` 0 makes it a list again. 1, or 0 with the level unchanged when `n`
 * exceeds ROLLTUI_MENU_COLUMNS, a width is outside 1..1000, or an alignment is neither ROLLTUI_MENU_ALIGN_LEFT nor _RIGHT: the
 * same rules a menu file's `columns` are held to. */
int rolltui_menu_item_set_columns(RolltuiMenuItem* level, const RolltuiMenuColumn* columns, size_t n);

/* ---- unicode -------------------------------------------------------------------------------*/

void rolltui_menu_event_release(RolltuiMenuEvent* e);

RolltuiMenuItem* rolltui_menu_root(RolltuiMenu* m);

/* Depth-first, any level; NULL when absent. A CHOICE's options are NOT searched: they are its VALUES, in their own id namespace, so an
 * option and a field may share an id and only the field is a thing to find. */
RolltuiMenuItem* rolltui_menu_find(RolltuiMenu* m, const char* id, size_t len);

/* The item the cursor is on, as a MOVED event names it - the dropdown's highlighted option while one is open, else the level's cursor
 * row - its id into `id`, REPLACED; empty where there is none (the back row, an empty level). A MOVED event says the cursor CHANGED; a
 * host that describes the focused item (a help line, an info panel) asks this for where it is before anything moved - a menu just
 * made, a window just given the focus - and after a rebuild that kept the cursor where it was. */
void rolltui_menu_focused(RolltuiMenu* m, RolltuiStr* id);

/* A Choice's options / a Submenu's items, by COPY, then the flat list and the selection are rebuilt. THIS IS WHAT MAKES A MENU DYNAMIC:
 * a file gives the levels, labels and action ids (the SKELETON), and a host FILLS the parts that depend on what exists at runtime by
 * building `RolltuiMenuItem`s and setting them here (roll's Theme, Layout and Bindings choices come from the preset store's live listing,
 * so a preset saved a moment ago appears with no file edited). Put the skeleton in the file and the runtime contents here. */
int rolltui_menu_set_options(RolltuiMenu* m, const char* id, size_t len, const RolltuiMenuItemList* options);

/* THE THREE PLAIN SETTERS, each `find` plus one assignment, 0 when no item has that id. Two hosts wrote the same three-line wrapper
 * around `Windows::menu()`'s result, so they live here. */
int rolltui_menu_set_value(RolltuiMenu* m, const char* id, size_t len, const char* value, size_t value_len);

int rolltui_menu_set_enabled(RolltuiMenu* m, const char* id, size_t len, int enabled);
/* A toggle's state — what a host sets from its own saved setting when the menu opens, so the
 * box reads the way the app already behaves. */
int rolltui_menu_set_checked(RolltuiMenu* m, const char* id, size_t len, int checked);

/* ---- navigation state --------------------------------------------------------------------------- */
void rolltui_menu_reset(RolltuiMenu* m);

/* "settings › theme", into a caller's string. */
void rolltui_menu_set_palette(RolltuiMenu* m, int on);

/* ---- the three tree walks ------------------------------------------------------------------
 * Pure over a `RolltuiMenuItem` tree, with no widget state: they take the ROOT rather than a `RolltuiMenu*`, so a host can call them
 * on a tree it has not mounted.
 * `apply_shortcuts` fills each item's `shortcut` from the live table. An action NO layout declares is INERT (the table keeps its
 * chords but nothing can emit it), so it gets an EMPTY shortcut: printing them would promise a key that cannot fire.
 * `item_actions` and `unknown_validators` ENUMERATE through a sink, so the caller owns whatever it collects into. `item_actions`
 * reports a PAIR per item, which is why it has its own two-string sink rather than `RolltuiPutFn`. */
void rolltui_menu_apply_shortcuts(RolltuiMenuItem* root, const RolltuiBindings* b);

void rolltui_menu_action_list_release(RolltuiMenuActionListRaw* l);

/* REPLACES `*out` (reusing its buffers). Depth-first, the tree's own order. */
void rolltui_menu_item_actions(const RolltuiMenuItem* root, RolltuiMenuActionListRaw* out);

/* The LIBRARY'S OWN fifteen (plus the input table they point at), so a consumer can call `handle` without spelling them. BORROWS static
 * storage. */
const RolltuiMenuActions* rolltui_menu_default_actions(void);

void rolltui_menu_handle(RolltuiMenu* m, const RolltuiEvent* e, const RolltuiBindings* bindings,
                         const RolltuiMenuActions* actions, RolltuiMenuEvent* out);

void rolltui_menu_load_report_release(RolltuiMenuLoadReport* r); /* frees everything; zeroes it */

int rolltui_menu_load_report_clean(const RolltuiMenuLoadReport* r);

/* Parses a WHOLE menu file's TEXT into `out`, which the caller owns (a default-constructed `RolltuiMenuItem` in C++, or
 * `rolltui_menu_item_init`'d in C): it FILLS it in place, releasing whatever `out` held first. Returns 0 only when `text` is
 * fundamentally unusable (a JSON syntax error, or the root is not an object): `report->error` says which and `out` is left freshly
 * empty. Otherwise 1, even when the file's own shape is wrong (a bad kind, a duplicate id, a root that is not a submenu, ...): those are
 * reported and `out` gets whatever the file was good for. `report` is reset on every call. */
int rolltui_menu_parse_json(const char* text, size_t len, RolltuiMenuItem* out, RolltuiMenuLoadReport* report);

/* ========================================================================================
 * effects — a host registers effect kinds and holds an effect map
 * ======================================================================================== */

/* Everything a kind is allowed to know about the cell it is answering for. A cell is about twice as tall as wide: an effect that
 * travels VERTICALLY moves in visual units (a row counts as two columns) or it visibly hurries. `index`, `length` and `fraction` below
 * are in cells; a kind that spans rows converts. */
typedef struct RolltuiEffectCell {
  unsigned long long elapsed_ms ROLLTUI_DEFAULT(0); /* since the span entered the state */
  int index ROLLTUI_DEFAULT(0);                     /* 0-based, within the span */
  int length ROLLTUI_DEFAULT(1);                    /* the span's length in cells */
  double fraction ROLLTUI_DEFAULT(0);               /* Progress: 0..1 */
  RolltuiStyle base;                                /* the cell's style as drawn */
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);  /* this terminal's East Asian ambiguous width */
} RolltuiEffectCell;

/* THE GLYPH IS INLINE, WITH A STATED REFUSAL rather than a spill: an effect glyph is one frame of a cycle a theme author or host
 * WROTE, so a bound is a constraint on that file, not on a user's data. Thirty-two bytes is past every emoji ZWJ sequence Unicode
 * defines; anything longer is REFUSED AND COUNTED in `glyphs_refused`, never silent. `glyph_len` is the length the kind ASKED for,
 * even past the cap, so the applier can tell "too long" from "fits". */
#define ROLLTUI_EFFECT_GLYPH_MAX 32

typedef struct RolltuiEffectOut {
  unsigned char has_glyph ROLLTUI_DEFAULT(0);
  unsigned char has_style ROLLTUI_DEFAULT(0);
  RolltuiStyle style;
  size_t glyph_len ROLLTUI_DEFAULT(0);
  char glyph[ROLLTUI_EFFECT_GLYPH_MAX];
} RolltuiEffectOut;

/* One frame of a glyph cycle: a BORROW of bytes the owning map keeps, valid for as long as
 * that map is not changed. */
typedef struct RolltuiEffectFrame {
  const char* bytes;
  size_t len;
} RolltuiEffectFrame;

/* ONE THEME SPEC, owned by the `RolltuiEffectMap` it lives in. Every pointer borrows that map's storage, valid until the map next
 * changes. */
typedef struct RolltuiEffectSpec {
  const char* kind;
  size_t kind_len;
  const unsigned char* roles; /* rolltui::Role values, one byte each */
  size_t role_count;          /* NEVER zero: the map substitutes the fallback it was handed */
  /* …and what the spec itself was GIVEN: 0 when it named none. Separate from `role_count` because a serialiser must write no "roles"
   * key for a spec that had none, and the substituted fallback looks like a spec that named that one role. The applier reads
   * `role_count`; a writer reads this. */
  size_t own_role_count;
  const RolltuiEffectFrame* frames;
  size_t frame_count;
  int period_ms; /* one full cycle; 0 or less: a STILL effect, no tick */
  int width;     /* shimmer: the sweeping window, in cells */
  int steps;     /* how many distinct pictures a period has (0 → the kind's own) */
  /* 0..100: how much a sweeping kind's SPEED varies from pass to pass, as a percentage; 0 (the default) is a metronome. THE VARIATION
   * IS DETERMINISTIC, derived from the pass number: an effect is a pure function of `(elapsed, index, length, fraction, style)`, which is
   * what lets `--tick N` make a moving frame a golden frame. */
  int jitter;
  unsigned char backward;
} RolltuiEffectSpec;

/* ---- what a THEME carries, owned in C -------------------------------------------------- */
/* A map of STATE -> the specs that state looks like while it lasts. `states` is the caller's state vocabulary size (the C never learns
 * a state's name); `fallback_role` is the role a spec with none of its own picks. OWNED, LONG-LIVED: one map per `rolltui::Theme`,
 * which frees it.
 * A BORROW, valid until the map next changes. NULL for an index past the state's specs. */

typedef struct RolltuiEffectReport {
  int marks_drawn;    /* marks the theme had an effect for */
  int cells_touched;
  int glyphs_refused; /* overrides dropped: wrong width, or past the glyph cap */
} RolltuiEffectReport;

/* `ctx` is what the host handed to `rolltui_effect_register`; `host` is what the caller handed to `rolltui_effects_apply`. The C
 * dereferences neither. */
typedef void (*RolltuiEffectFn)(void* ctx, const RolltuiEffectSpec* spec, const RolltuiStyle* styles,
                                const void* host, const RolltuiEffectCell* in, RolltuiEffectOut* out);

/* ---- THE EFFECT-STATE VOCABULARY -----------------------------------------------------------
 * Same move as `ROLLTUI_ROLE_LIST`: one list that both spellings expand. A widget MARKS a span with a state and stops; the theme maps
 * state -> effect as data in its file, by NAME, so the names must be reachable from the C that reads it.
 * ORDER IS ABI: `none` stays 0, because a zeroed mark means "not marked". */
/* THREE SPELLINGS, ONE LIST: the name a theme FILE uses ("waiting"), the C constant (ROLLTUI_EFFECT_STATE_WAITING) and the C++
 * identifier (EffectState::Waiting). */
/* A THEME FILE maps these state names to effects, and a DOCUMENT marks a span with one (`<!-- state: waiting -->`). A theme that maps
 * nothing is a still UI, the default. Shipped files: `rolltui/presets/themes/`.
 * These are RUNG 1. A host whose widget has states of its own registers them by name (`rolltui_effect_state_register`), marks with the
 * index it is handed, and a theme file (or the app's own mapping, merged with `rolltui_theme_effects_merge`) maps them under `effects`
 * by that name; `dirktui` is the worked example.
 * `streaming` and `streamed` are two states: a span that IS arriving and one that HAS arrived are different claims. An effect on
 * `streamed` runs while the span is on screen: `rolltui_effects_tick_ms` reads the marks in the FRAME, so a span scrolled out of view
 * asks for no wakeup, and scrolled back it resumes where it would have been. */
#define ROLLTUI_EFFECT_STATE_LIST(X) \
  X(none, NONE, None) \
  X(waiting, WAITING, Waiting) \
  X(streaming, STREAMING, Streaming) \
  X(progress, PROGRESS, Progress) \
  X(flash, FLASH, Flash) \
  X(streamed, STREAMED, Streamed) \
  X(picker_cursor, PICKER_CURSOR, PickerCursor) \
  X(picker_trail, PICKER_TRAIL, PickerTrail) \
  X(picker_opened, PICKER_OPENED, PickerOpened)

typedef enum RolltuiEffectState {
#define ROLLTUI_EFFECT_STATE_ENUM_(lower, UPPER, Camel) ROLLTUI_EFFECT_STATE_##UPPER,
  ROLLTUI_EFFECT_STATE_LIST(ROLLTUI_EFFECT_STATE_ENUM_)
#undef ROLLTUI_EFFECT_STATE_ENUM_
  ROLLTUI_EFFECT_STATE_COUNT
} RolltuiEffectState;

#define ROLLTUI_EFFECT_OK 0

#define ROLLTUI_EFFECT_NO_NAME 1

#define ROLLTUI_EFFECT_NO_FN 2

#define ROLLTUI_EFFECT_IS_BUILTIN 3 /* rung 1 is never shadowed */

#define ROLLTUI_EFFECT_DUPLICATE 4

/* ---- working memory -------------------------------------------------------------------- */
/* The grapheme buffer and Unicode scratch the glyph kinds need to measure their own frames. One per thread, made once and grown over
 * the first few calls, like `RolltuiUnicodeScratch`. */
typedef struct RolltuiEffectScratch RolltuiEffectScratch;

/* Called once per kind the map names that NOTHING answers for, with its name as a borrow valid for the call. A host says it out
 * loud; the C never judges a kind. */
typedef void (*RolltuiEffectUnknownFn)(void* ctx, const char* kind, size_t len);

/* ---- effects -------------------------------------------------------------------------------*/

void rolltui_effect_map_free(RolltuiEffectMap* m);

int rolltui_effect_map_empty(const RolltuiEffectMap* m);

RolltuiEffectScratch* rolltui_effect_scratch_new(void);

void rolltui_effect_scratch_free(RolltuiEffectScratch* s);

/* ---- REGISTERING: a host's own kinds and a host's own states ----------------------------------
 * A KIND is how a span moves; a STATE is what a widget says about a span. The library ships seven kinds and six states, both named
 * for a transcript, and both are rung 1 of a two-rung table a host extends by registration (rung 1 first and never shadowed, then
 * the host's in registration order, as for a widget kind).
 * States are registrable because a file browser marking its cursor row `streaming` so a theme can move it would be two readings of
 * one name. A host registers `dirk.folder`, marks with the index it is handed, and a theme file maps `"dirk.folder"` under `effects`
 * as it maps `"waiting"`. The vocabulary a theme file is read against is then the SESSION's (`rolltui_theme_vocab(ctx)`); a host
 * that registers nothing gets the library's.
 * Registration takes ownership of nothing but a copy of the name. Both return one of the ROLLTUI_EFFECT_* codes below; a state
 * registration also hands back the index to mark with (on DUPLICATE, the index the name already has). */
int rolltui_effect_register(RolltuiContext* c, const char* name, size_t name_len, RolltuiEffectFn fn, void* ctx,
                            void (*free_ctx)(void*));
int rolltui_effect_state_register(RolltuiContext* c, const char* name, size_t name_len, int* out_state);

/* A mark names its state as an int the frame stored and never interpreted, so the C indexes the map with it and knows nothing about the
 * state vocabulary; a state index outside the map's own `states` draws nothing. Called by a host AFTER the whole screen has composed and
 * before the frame diff. `now_ms` is any monotonic millisecond clock. `rep` may not be NULL; `on_unknown` may. `c` is the SESSION whose
 * registered effect kinds rung 2 resolves against; NULL means the library's closed seven only. */
void rolltui_effects_apply(const RolltuiContext* c, RolltuiFrame* f, RolltuiEffectScratch* s,
                           const RolltuiStyle* styles, const void* host,
                           const RolltuiEffectMap* map, unsigned long long now_ms, int ambiguous_wide,
                           RolltuiEffectReport* rep, RolltuiEffectUnknownFn on_unknown, void* unknown_ctx);

/* The interval at which this frame must be redrawn for its motion, or 0 when nothing is
 * marked, when the theme maps nothing to what is marked, or when nothing mapped MOVES.
 * Clamped to at least 16 ms so a long span cannot ask for a wakeup per millisecond. */
int rolltui_effects_tick_ms(const RolltuiContext* c, const RolltuiFrame* f, const RolltuiEffectMap* map);

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
} RolltuiTranscriptOptions;

/* ---- one entry's cached layout ---------------------------------------------------------------- */
/* `rolltui::EntryLayout` IS this struct. It OWNS one span store: the markdown render puts the entry's BODY lines at the front, and this
 * layout's own drawn lines (the body behind the entry's prefix, with the fold summary above) are appended after and REFERENCE the
 * body's spans by index. `body` is where the drawn ones begin. */
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
} RolltuiTextPos;

/* ---- forward declarations the C++ members just below call ----------------------------------*/
int rolltui_text_pos_less(const RolltuiTextPos* a, const RolltuiTextPos* b);


/* RolltuiSelectionRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiSelection : public
 * RolltuiSelectionRaw` reclaiming the name, with empty()/first()/last(). (Not `range_in`: declared
 * here for years with no definition anywhere and no caller — the real work already happens
 * through `rolltui_selection_range_in`, c/rolltui_widget_transcript.h — dropped rather than moved.) */
typedef struct RolltuiSelectionRaw {
  RolltuiTextPos anchor, head;
  unsigned char active ROLLTUI_DEFAULT(0);
} RolltuiSelectionRaw;

/* One find hit, in the same logical space as a position. */
typedef struct RolltuiFindMatch {
  size_t entry ROLLTUI_DEFAULT(0);
  size_t offset ROLLTUI_DEFAULT(0);
  size_t length ROLLTUI_DEFAULT(0);
} RolltuiFindMatch;

typedef struct RolltuiTranscriptStats {
  long layout_us ROLLTUI_DEFAULT(0);      /* the last layout() call */
  size_t entries_relaid ROLLTUI_DEFAULT(0);
  size_t total_lines ROLLTUI_DEFAULT(0);
  size_t cache_size ROLLTUI_DEFAULT(0);
} RolltuiTranscriptStats;

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

/* THE HOST'S CLIPBOARD, as a function pointer and a context. NULL turns it off. */
void rolltui_transcript_set_copy(RolltuiTranscript* t, RolltuiCopyFn fn, void* ctx);

/* The LIBRARY'S OWN eleven. BORROWS static storage. */
const RolltuiTranscriptActions* rolltui_transcript_default_actions(void);

int rolltui_transcript_handle(RolltuiTranscript* t, const RolltuiEvent* e, const RolltuiDocumentRaw* doc,
                              unsigned long long now_ms, const RolltuiBindings* bindings,
                              const RolltuiTranscriptActions* actions);

/* True while a drag holds the pointer outside the area: tick at ~50 ms and re-layout. */
int rolltui_transcript_wants_tick(const RolltuiTranscript* t);

void rolltui_transcript_tick(RolltuiTranscript* t);

/* ---- scrolling --------------------------------------------------------------------------------------- */
void rolltui_transcript_scroll_page(RolltuiTranscript* t, int direction);

void rolltui_transcript_scroll_to_top(RolltuiTranscript* t);

void rolltui_transcript_scroll_to_bottom(RolltuiTranscript* t);

/* ---- folding ------------------------------------------------------------------------------------------ */
int rolltui_transcript_toggle_fold_nearest_top(RolltuiTranscript* t, const RolltuiDocumentRaw* doc);

/* ---- find -------------------------------------------------------------------------------------------- */
/* "" clears. Never scrolls: the reveal it asks for happens in layout(). 1 when it CHANGED. */
int rolltui_transcript_set_query(RolltuiTranscript* t, const char* q, size_t len);

/* The current match's 1-BASED position, for "3/17"; 0 when there is none. */
int rolltui_transcript_find_next(RolltuiTranscript* t);

int rolltui_transcript_find_prev(RolltuiTranscript* t);

/* ---- selection ----------------------------------------------------------------------------------------- */
/* The logical position under screen cell (x, y). 0 only for an empty document or a row above
 * the area. */
int rolltui_transcript_copy_selection(RolltuiTranscript* t);
/* How many rows the whole document takes at the width it was last laid out at: what an inline host sizes its region to. */
size_t rolltui_transcript_total_lines(const RolltuiTranscript* t);
/* ONE ENTRY drawn as this transcript draws it (its prefix, Markdown, code blocks and diagrams, its highlighter and options) into a
 * frame of its own, `width` wide and as tall as the entry plus the transcript's gap below it, filled with `fill` first: the rows an
 * inline host commits when a message is finished (`rolltui_terminal_commit`), spaced as the transcript spaces its entries. `styles`
 * is indexed by role, as for a draw. OWNED: `rolltui_frame_free`. */
RolltuiFrame* rolltui_transcript_entry_frame(const RolltuiTranscript* t, const RolltuiDocEntryRaw* e, int width,
                                             const RolltuiStyle* styles, RolltuiStyle fill);

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

typedef struct RolltuiWidgetPlugin {
  /* ---- REQUIRED (rule 2) ---- */
  /* Frees `ctx`. The window table calls this and nothing else ever does. */
  void (*destroy)(void* ctx);
  void (*layout)(void* ctx, const RolltuiResolvedNode* rn);
  void (*draw)(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f);

  /* ---- OPTIONAL: NULL means the behaviour stated on the line (rule 2) ---- */

  /* Why this widget cannot draw, into `out`; 0 when it can. NULL: it always can. */
  int (*problem)(void* ctx, RolltuiStr* out);
  /* Note `i`, into `out`; 0 when there is no i-th note. A note does NOT stop the widget drawing: a menu file's unknown key is named in
   * the report and the menu still shows. NULL: no notes. */
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
  /* The window's TITLE this frame, into `out`, given the layout's own (`given`, possibly empty): what only the widget knows, said after
   * what the author said (a menu's title is the window's title plus the level's path, "settings › Sort by"). 0 keeps the layout's title.
   * Read once per frame by `rolltui_windows_autosize`. NULL: the layout's title. */
  int (*title)(void* ctx, const char* given, size_t given_len, RolltuiStr* out);
  /* Closes ONE inner level of the widget's own (a dropdown, a menu level, a field being edited) and answers 1; 0 when none is open. The
   * close key and a dismissing press outside a popup ask the focused widget this FIRST, so they close one level at a time and the popup
   * last. NULL: the widget has no levels. */
  int (*back)(void* ctx);
  /* THE KEY SCOPE this widget reads its keys in, into `*scope`; answers 1 while a digit typed into it is a COUNT (`5` then Down moves
   * five) and 0 while it is text or a jump. The window table holds a key sequence (`g g`) and a count for the widget with this, and
   * hands it the action they made. NULL: no sequences, no counts. */
  int (*keys)(void* ctx, const char** scope, size_t* len);
  /* Does what a key bound to `action` does; 1 when it did something. Asked only for an action no single chord is bound to: the table
   * hands the widget that chord otherwise. NULL: such an action does nothing here. */
  int (*perform)(void* ctx, const char* action, size_t len);
  /* The window's FOOTER this frame, into `out`: what only the widget knows, at the right of its bottom border (a list's "12 of 340").
   * 0 keeps the layout's own. Read once per frame by `rolltui_windows_autosize`. NULL: the layout's. */
  int (*footer)(void* ctx, RolltuiStr* out);
  /* Whether this widget, focused, TAKES `key` itself: the program's own keys (`rolltui_windows_app_key`) never get a key the window
   * with focus takes. 1 for a key bound in the widget's own scopes, and for any key while something in it is being typed into or is
   * waiting for a key. NULL: a key bound in the `keys` scope. */
  int (*takes)(void* ctx, const RolltuiChord* key);
} RolltuiWidgetPlugin;

/* One widget: what it IS and how to talk to it. The plugin is a BORROW of a table the
 * implementor keeps (a `static const` per kind); `ctx` is OWNED by whoever holds this. */
typedef struct RolltuiWidget {
  const RolltuiWidgetPlugin* vt;
  void* ctx;
} RolltuiWidget;

/* Builds a widget for `content` (the whole string, "kind:source"). Returns a widget whose `ctx` the table then OWNS, or a zeroed one
 * for "I cannot build this" (not an error path: `Windows` draws the error panel and names it in the report). A kind is a session's and
 * an instance a screen's, so a factory is handed BOTH: `ctx` is what was registered with the kind, `w` the screen the instance is built
 * for (the built-in `transcript` kind lives in that screen's map). */
typedef RolltuiWidget (*RolltuiWidgetFactory)(void* ctx, RolltuiWindows* w, const char* content, size_t len);

/* The widget a WINDOW holds, or NULL — filled by `sync`. */

/* ---- THE TYPED WIDGETS, OWNED HERE AND REACHABLE BY NAME ---------------------------------
 * `rolltui_windows_at` / `_widget_for` hand back an opaque `RolltuiWidget{vt, ctx}`: enough to draw a widget and route an event at
 * it. The typed accessors below read the SAME table the built-in factories build from, so there is one object per source by
 * construction (two views of one source silently diverging is the failure this prevents). Created on demand and kept until `w` is
 * freed; every one comes back fully formed: an input with the library's defaults, a transcript with the library's roles and the last
 * highlighter set, a menu with its single-line editor and its file resolved now. */

/* Forward declarations: `RolltuiRows`' own inline C++ methods below call these before their
 * full declarations (right after the struct) would otherwise be seen. */
typedef struct RolltuiRows RolltuiRows;

/* rows: one row of a `rows:` window — a label column and a value that wraps under it.
 * `rolltui::Row` IS this struct. */
typedef struct RolltuiRow {
  RolltuiStr label, value;
  /* A row whose value IS a colour (`rolltui_rows_add_colour`): `value` holds its spelling and the row is
   * drawn with `rolltui_frame_put_swatch`'s square before it. `has_swatch` is 0 for every other row. */
  RolltuiStyleColorRaw swatch;
  unsigned char has_swatch;
} RolltuiRow;

/* ---- forward declarations the C++ members just below call ----------------------------------*/
void rolltui_rows_add(RolltuiRows* r, const char* label, size_t label_len, const char* value, size_t value_len);
void rolltui_rows_add_colour(RolltuiRows* r, const char* label, size_t label_len, RolltuiStyleColorRaw colour);
void rolltui_rows_release(RolltuiRows* r);
void rolltui_rows_reset(RolltuiRows* r);

/* WHAT A HOST FILLS instead of returning a fresh vector every frame. `rolltui_rows_reset` keeps the array's capacity AND every row's
 * string buffers, so `rolltui_rows_add` on a warm frame allocates nothing. `rolltui::Rows` IS this struct. */
typedef struct RolltuiRows {
  RolltuiRow* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);   /* rows live; v[0..n) */
  size_t cap ROLLTUI_DEFAULT(0); /* rows allocated — v keeps its storage past n */
#ifdef __cplusplus
  void reset() { rolltui_rows_reset(this); }
  // rolltui's own shapes only: a pointer and a length, or a RolltuiStr.
  void add(const char* label, std::size_t label_len, const char* value, std::size_t value_len) {
    rolltui_rows_add(this, label, label_len, value, value_len);
  }
  void add(const char* label, const char* value) { rolltui_rows_add(this, label, std::strlen(label), value, std::strlen(value)); }
  void add(const char* label, const char* value, std::size_t value_len) { rolltui_rows_add(this, label, std::strlen(label), value, value_len); }
  void add(const char* label, const RolltuiStr& value) { rolltui_rows_add(this, label, std::strlen(label), value.p, value.n); }
  void add(const RolltuiStr& label, const RolltuiStr& value) { rolltui_rows_add(this, label.p, label.n, value.p, value.n); }
  // A row whose value is a colour: its spelling, with its swatch before it wherever rows are drawn.
  void add_colour(const char* label, RolltuiStyleColorRaw colour) { rolltui_rows_add_colour(this, label, std::strlen(label), colour); }
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

/* submit: `on_submit` is `Windows::OnSubmit` as an int (0 SendAndClear, 1 Keep), bound with the callable because a host always sets
 * both together. */
typedef void (*RolltuiSubmitFn)(void* ctx, const char* text, size_t len);

/* note: an input's one-line note and the STATE it is in. A host with no motion to report fills only `text`; `state` defaults to None, so
 * a bare string converts to "no motion". `rolltui::Note` IS this struct.
 * RolltuiNoteRaw: the plain data. `rolltui_cpp.h` declares `class RolltuiNote : public RolltuiNoteRaw`
 * reclaiming the name, with the converting constructors and `set()`/`operator=`. */
typedef struct RolltuiNoteRaw {
  RolltuiStr text;
#ifdef __cplusplus
  rolltui::EffectState state = static_cast<rolltui::EffectState>(0); /* None */
#else
  unsigned char state;
#endif
  unsigned long long since_ms ROLLTUI_DEFAULT(0); /* when it entered `state` */
} RolltuiNoteRaw;

/* note: `out` is the caller's `RolltuiNote`, already cleared, filled in place. */
typedef void (*RolltuiNoteFn)(void* ctx, RolltuiNoteRaw* out);

/* gauge: how far something has got, which a `gauge:` window draws on one row — the label, a bar and the percent; a spinner and the
 * label while how far is not known. `label` is OWNED; `rolltui_gauge_release` frees it (C++ frees it itself). */
typedef struct RolltuiGauge {
  double fraction ROLLTUI_DEFAULT(0);             /* 0..1 done; below 0, not known */
  RolltuiStr label;                               /* what is being done: "copying 3 files" */
  unsigned char failed ROLLTUI_DEFAULT(0);        /* it stopped short: the bar and the percent in the error role */
  unsigned long long since_ms ROLLTUI_DEFAULT(0); /* when it began, on the clock the env's `now_ms` reads: the spinner's phase */
} RolltuiGauge;
void rolltui_gauge_release(RolltuiGauge* g);
/* gauge: `out` is the caller's `RolltuiGauge`, its label cleared, filled in place. */
typedef void (*RolltuiGaugeFn)(void* ctx, RolltuiGauge* out);

/* count: a number shown big, which a `count:` window draws as a dashboard panel: the legend centred on its top row (in the focus
 * colours while the window has the focus), then the count in seven-segment digits three rows tall, a second count after a divider,
 * and a trend arrow. One line when the window is too small for that. `label` is OWNED; `rolltui_count_release` frees it. */
typedef struct RolltuiCount {
  RolltuiStr label;                        /* the legend: "pods" */
  long long value ROLLTUI_DEFAULT(0);      /* the count, in the `ok` role */
  long long fault ROLLTUI_DEFAULT(-1);     /* a second count after the divider, in the error role ("3 failing"); below 0, none */
  unsigned char digits ROLLTUI_DEFAULT(0); /* at least this many digits each, the leading zeros dimmed */
  signed char trend ROLLTUI_DEFAULT(0);    /* 1 rising, -1 falling: an arrow after the counts; 0 none */
} RolltuiCount;
void rolltui_count_release(RolltuiCount* c);
/* count: `out` is the caller's `RolltuiCount`, reset (its label cleared, `fault` -1), filled in place. */
typedef void (*RolltuiCountFn)(void* ctx, RolltuiCount* out);

typedef struct RolltuiWidgetEnv {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  unsigned long long now_ms ROLLTUI_DEFAULT(0);
  /* THE PERSON ASKED FOR NO MOTION: what the library moves by itself (a toast sliding in) is placed where it ends at once. */
  unsigned char still ROLLTUI_DEFAULT(0);
  /* THE WHEEL'S STEP: the lines a notch scrolls in every window that scrolls by lines (a menu moves a row); 0: 3. The person's. */
  unsigned char wheel_lines ROLLTUI_DEFAULT(0);
} RolltuiWidgetEnv;

/* THE TWO ROLES THE WINDOW ITSELF DRAWS WITH: the scrollbar's track and thumb, which live in the window's border column and which a
 * widget never sees. */
typedef struct RolltuiWindowRoles {
  unsigned char scrollbar;
  unsigned char border;
  unsigned char border_active;
} RolltuiWindowRoles;

/* ---- layout --------------------------------------------------------------------------------*/

RolltuiWindows* rolltui_windows_new(RolltuiContext* ctx);

/* The session this Windows was made for — BORROWED, and it must outlive the Windows. */
RolltuiContext* rolltui_windows_context(const RolltuiWindows* w);

void rolltui_windows_free(RolltuiWindows* w);

/* Registers a kind NAME with the factory that builds it. The library's own seven go through this call at construction, as a host's
 * does (rule 5), with `free_ctx` NULL since their `ctx` is the `Windows` object itself. A host's own kind passes a real `free_ctx`,
 * which runs when the row is replaced (a second `register_kind` for the same name) and at `rolltui_windows_free`. The layout
 * vocabulary's half of the registration, and the refusal to shadow a library kind, is `rolltui_widget_kind_register` in
 * `rolltui_layout.h`. */
void rolltui_context_register_kind(RolltuiContext* ctx, const char* name, size_t len,
                                   RolltuiWidgetFactory factory, void* kind_ctx, void (*free_ctx)(void*));

/* THE SYNTAX HIGHLIGHTER every transcript this table owns renders code blocks through. A table starts with the library's own
 * languages: the shipped ones and the person's from `<config>/rolltui/syntax/`, read when the first code line is drawn. A host's
 * highlighter replaces them, and `fn` NULL turns highlighting off. Pushed onto every transcript that exists AND onto each one
 * created afterwards, so the order a host calls this and `rolltui_windows_transcript` in cannot matter. `ctx` is released through
 * `free_ctx` when this is called again or when `w` is freed. */
void rolltui_windows_set_highlight(RolltuiWindows* w, RolltuiMdHighlightFn fn, void* ctx,
                                   void (*free_ctx)(void*));

/* THE EXTRA ROWS an `input:<source>` window must have whatever its text says (roll holds the prompt as tall as the modal placed over
 * it). Per-WINDOW sizing the widget keeps, not part of the edited text the input owns, so it is set here by source name. The widget is
 * created on demand, so a host may set the floor before any window has shown this input. */
void rolltui_windows_set_input_min_outer(RolltuiWindows* w, const char* source, size_t len, int rows);

/* documents: `doc` is a BORROW this table never frees; the host (or the table, for a sample built from markdown) keeps it alive. What
 * crosses is `&doc->entries`, the real `RolltuiDocument` the transcript kind reads directly. */
void rolltui_windows_bind_document(RolltuiWindows* w, const char* name, size_t len, const RolltuiDocumentRaw* doc);

/* …and the OWNED half: one entry of verbatim markdown this table keeps, for a host with no live `RolltuiDocument` to point at (sample
 * content in a preview, a fixed page of help, a C consumer showing one line). Binding the same name twice replaces the sample. The
 * `RolltuiDocument` is heap-held for the table's life, because the borrow above needs a stable address. */
void rolltui_windows_bind_sample_document(RolltuiWindows* w, const char* name, size_t len, const char* markdown,
                                          size_t markdown_len);

void rolltui_windows_bind_rows(RolltuiWindows* w, const char* name, size_t len, RolltuiRowsFn fn, void* ctx,
                               void (*free_ctx)(void*));

void rolltui_windows_bind_submit(RolltuiWindows* w, const char* name, size_t len, RolltuiSubmitFn fn, void* ctx,
                                 void (*free_ctx)(void*), int on_submit);

void rolltui_windows_bind_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNoteFn fn, void* ctx,
                               void (*free_ctx)(void*));

/* changed: `fn` with the text, each time an `input:<name>` window's text changes (a key typed or erased, a paste, an undo), before
 * anything is drawn: filtering as a person types. Debounce it with `rolltui_run_debounce` to act once a burst of keys is over. */
void rolltui_windows_bind_changed(RolltuiWindows* w, const char* name, size_t len, RolltuiSubmitFn fn, void* ctx,
                                  void (*free_ctx)(void*));

void rolltui_windows_bind_gauge(RolltuiWindows* w, const char* name, size_t len, RolltuiGaugeFn fn, void* ctx,
                                void (*free_ctx)(void*));
void rolltui_windows_bind_count(RolltuiWindows* w, const char* name, size_t len, RolltuiCountFn fn, void* ctx,
                                void (*free_ctx)(void*));

/* output: another program's output (`RolltuiAnsiText`, filled by `rolltui_child_pump` or `rolltui_ansi_feed`) shown in an
 * `output:<name>` window, which follows the end as it grows unless it was scrolled back. `text` is a BORROW the host keeps alive
 * while it is bound; binding again replaces it. Lines wrap at the window's edge unless `set_output_wrap` says 0, when the window
 * scrolls sideways (the wheel's left and right). */
void rolltui_windows_bind_output(RolltuiWindows* w, const char* name, size_t len, const RolltuiAnsiText* text);
/* AN OUTPUT WINDOW'S WAYS. `timestamps`: each line's time (HH:MM:SS, from `rolltui_ansi_set_stamp`) in a gutter before it. `status`:
 * the footer says "following", or "paused · N below" once scrolled back, and whether a program run into it is running or how it
 * exited. `filter` (COPIED; NULL or "" none): only the lines it matches, the lists' filter, are shown, their matches bold and
 * underlined; rules always show. */
typedef struct RolltuiOutputOptions {
  unsigned char wrap ROLLTUI_DEFAULT(1);
  unsigned char timestamps ROLLTUI_DEFAULT(0);
  unsigned char status ROLLTUI_DEFAULT(0);
  const char* filter ROLLTUI_DEFAULT(ROLLTUI_NULL);
} RolltuiOutputOptions;
void rolltui_windows_set_output_options(RolltuiWindows* w, const char* name, size_t len, const RolltuiOutputOptions* o);

/* THE WORKERS the built-in kinds read files on, BORROWED (the host keeps them for the table's life): the column browser's preview
 * reads a file and decodes a picture there while the cursor moves on, says "loading…" once a read has taken 100 ms, and drops a
 * read the cursor left. NULL (the default) reads on the drawing thread, which is what a frame dump does. */
void rolltui_windows_set_tasks(RolltuiWindows* w, RolltuiTasks* tasks);
/* INPUT HELD while work the person waits on runs: no window gets a key or the pointer, and a box with a spinner, `label` and
 * "Esc cancels" sits over the screen. Escape cancels `job` and lets go at once; the hold also ends by itself when `job` (on the
 * tasks above) has ended, or at `rolltui_windows_release`. `job` 0: only Escape or the release end it. The host still sees every
 * key first, so its own keys (quit) keep working. A new hold replaces the one before. */
void rolltui_windows_hold(RolltuiWindows* w, const char* label, size_t len, unsigned int job);
void rolltui_windows_release(RolltuiWindows* w);
int rolltui_windows_held(RolltuiWindows* w);

/* image: a picture the host made, shown in an `image:<name>` window, centred, instead of the file that name would be read from.
 * `img` is a BORROW the host keeps alive while it is bound; NULL unbinds it. An `image:` window with nothing bound reads the file
 * its source names (relative to the preset directory, as `file:` does) and reads it again when it changes. */
void rolltui_windows_bind_image(RolltuiWindows* w, const char* name, size_t len, const RolltuiImage* img);
void rolltui_windows_set_output_wrap(RolltuiWindows* w, const char* name, size_t len, int wrap);

/* ---- progress a host draws itself ---------------------------------------------------------- */
/* A GAUGE ON ONE ROW, as a `gauge:` window draws it, for a host's own widget (a status line, a row of a list): the label, a bar of
 * eighth blocks and the percent, the label dropped first when the row is narrow; a spinner and the label while the fraction is below
 * 0. Drawn in the table's roles (`styles` indexed by role). Returns the cells used. */
int rolltui_gauge_draw(RolltuiFrame* f, RolltuiDrawScratch* draw, int x, int y, int width, const RolltuiGauge* g,
                       const RolltuiStyle* styles, int ambiguous_wide);
/* BIG DIGITS, three rows tall from (x, y): 0-9 as seven segments of heavy box drawing three cells wide, `-`, `.`, `:` and a space,
 * a cell between each; another character is drawn as it is on the middle row. `leading` (may be NULL) is the style of the zeros
 * before the first other digit, the last digit aside: a count padded to a width with its padding dimmed. Where ambiguous glyphs are
 * two cells wide the segments are ASCII (`_` and `|`). The width is the cells it takes, drawn or not. */
int rolltui_big_digits_width(const char* text, size_t len);
int rolltui_big_digits_draw(RolltuiFrame* f, int x, int y, const char* text, size_t len, RolltuiStyle style, const RolltuiStyle* leading,
                            int ambiguous_wide);
/* A SPINNER IN ONE CELL: a still glyph marked `waiting` since `since_ms`, which the theme's `waiting` effect turns (every shipped theme
 * maps it; a theme that maps nothing shows it still). A row of a list whose operation is running carries one. */
void rolltui_spinner_draw(RolltuiFrame* f, int x, int y, RolltuiStyle style, unsigned long long since_ms);

/* ---- messages: what the app tells the person ---------------------------------------------------
 * A host POSTS a message. The table keeps it in a history (the newest last, at most ROLLTUI_MESSAGE_HISTORY) and shows it as a TOAST,
 * a box in the screen's bottom-right corner, for ROLLTUI_TOAST_MS (an error twice as long), the newest at the bottom and the stack at
 * most a third of the screen tall. A toast slides in from the right unless the env is `still`. A press on one dismisses it. A
 * `messages` window lists the history; one row tall, it shows the newest message while its toast would, which is a status line's
 * message. Each level is drawn in its own role and glyph, so it reads without colour. */
#define ROLLTUI_MESSAGE_INFO 0    /* accent_1, i */
#define ROLLTUI_MESSAGE_OK 1      /* accent_2, a check */
#define ROLLTUI_MESSAGE_WARNING 2 /* warning, ! */
#define ROLLTUI_MESSAGE_ERROR 3   /* error, a cross */
#define ROLLTUI_TOAST_MS 6000
#define ROLLTUI_MESSAGE_HISTORY 500
void rolltui_windows_post(RolltuiWindows* w, int level, const char* text, size_t len, unsigned long long now_ms);
size_t rolltui_windows_message_count(const RolltuiWindows* w);
/* Message `i`, the oldest first: its text as a BORROW valid until the next post, its level and when it was posted. NULL past the end.
 * Any out-param may be NULL. */
const char* rolltui_windows_message_at(const RolltuiWindows* w, size_t i, size_t* len, int* level, unsigned long long* at_ms);
/* DRAWS THE TOASTS over `screen`, AFTER everything else in the frame. Returns how long until they or a window next change (a toast
 * ends or slides, a list scrolls at its edge under a drag, a "loading" shows, a changed row settles): milliseconds, or -1 when
 * nothing is due. A render returns the smaller of this and its own wait. */
int rolltui_windows_draw_toasts(RolltuiWindows* w, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles,
                                unsigned long long now_ms);
/* A press on a toast dismisses it: 1 when `e` was one, and the host stops there. Ask before routing a mouse event to the windows. */
int rolltui_windows_toast_event(RolltuiWindows* w, const RolltuiEvent* e);
void rolltui_windows_dismiss_toasts(RolltuiWindows* w);
/* THE DRAW TIMES, on screen: each window's top border carries how long its widget took to draw in the last frame (" 0.4 ms "),
 * muted, and bold in the warning role past half of a frame at 60 a second. Off by default; `ROLLTUI_TIMING=1` in the environment
 * turns it on for every window table, so any program can be looked at without a change to it. */
void rolltui_windows_set_timing(RolltuiWindows* w, int on);
/* A PRESS ON A LINK in a window (an OSC 8 hyperlink another program wrote, a Markdown link, one a host drew with
 * `rolltui_frame_link_id`) is offered to `fn` first, with the window and the URL, and 1 says the host took it: its own scheme, an
 * editor's or a program's internal link. Otherwise the library opens an http, https, mailto or file link with what the system opens
 * it with (`rolltui_open_with_system`), and a link of any other scheme does nothing. While the terminal reports hover, the link under
 * the pointer is underlined across its cells. `fn` NULL: the library's opening alone. */
typedef int (*RolltuiWindowsLinkFn)(void* ctx, const char* window, size_t window_len, const char* url, size_t url_len);
void rolltui_windows_set_link_handler(RolltuiWindows* w, RolltuiWindowsLinkFn fn, void* ctx);

/* ---- dialogs: a question the app asks the person ------------------------------------------------
 * A DIALOG, modal over the screen: the title in its border after its level's glyph (ROLLTUI_MESSAGE_*; the border and title in the
 * level's role), a body wrapped to its width, the items it is about (one line each, scrolling), and a row of framed buttons. A
 * button is chosen by `dialog.next` and `dialog.prev` (Tab, Shift-Tab, Right and Left) and answered by `dialog.answer` (Enter), by
 * its letter (the first letter of its label no button before it has, underlined) or by a press on it; the close key answers the
 * cancel button. `dialog.up`, `dialog.down`, `dialog.page_up` and `dialog.page_down` scroll the items, and `dialog.copy` hands the
 * host its text. With a PHRASE, a field under the body takes what is typed, and every button but the cancel one answers only once the
 * field holds the phrase exactly; until then they are drawn inert and letters are typed, not answers. It is as tall as what it holds,
 * at most nine tenths of the screen; FULL_SCREEN fills the screen, as an error page. */
typedef struct RolltuiDialog RolltuiDialog;
#define ROLLTUI_DIALOG_FULL_SCREEN 1u /* the dialog's: it fills the screen */
#define ROLLTUI_DIALOG_DEFAULT 1u     /* a button's: chosen when the dialog opens (else the first) */
#define ROLLTUI_DIALOG_CANCEL 2u      /* a button's: what the close key answers (else the last) */
/* OWNED: freed by `rolltui_dialog_free`, unless it is handed to `rolltui_windows_ask`. */
RolltuiDialog* rolltui_dialog_new(int level, unsigned flags, const char* title, size_t title_len, const char* body, size_t body_len);
void rolltui_dialog_add_item(RolltuiDialog* d, const char* text, size_t len);
void rolltui_dialog_add_button(RolltuiDialog* d, const char* label, size_t len, unsigned flags);
void rolltui_dialog_set_phrase(RolltuiDialog* d, const char* phrase, size_t len);
void rolltui_dialog_free(RolltuiDialog* d); /* a no-op on NULL */
/* OPENS `d`, OWNED by the table from the call, as a popup on `s` under `id`, focused, replacing an open dialog of that id. With no
 * buttons it has one, "OK". Sync the table straight after, as after `rolltui_window_stack_push_popup`. 1, or 0 when an argument is
 * missing (`d` is freed). */
int rolltui_windows_ask(RolltuiWindows* w, RolltuiWindowStack* s, const char* id, size_t len, RolltuiDialog* d);
/* WHAT THE DIALOG `id` SAYS, asked after each event is routed and delivered:
 *   ROLLTUI_DIALOG_ANSWERED  answered: the button's index in `*button` (may be NULL), the cancel button's when the close key or the
 *                            host closed it; the dialog is closed and gone. Told once.
 *   ROLLTUI_DIALOG_COPY      `dialog.copy` was pressed: its title, body and items in `*copy` (REPLACED; may be NULL), for
 *                            `rolltui_clipboard_copy`. The dialog stays open.
 *   0                        open and unanswered, or no dialog of that id. */
#define ROLLTUI_DIALOG_ANSWERED 1
#define ROLLTUI_DIALOG_COPY 2
int rolltui_windows_answer(RolltuiWindows* w, RolltuiWindowStack* s, const char* id, size_t len, int* button, RolltuiStr* copy);

/* the preset directory: `file:`, `menu:`'s user rung and `menus/<name>.json` resolve against
 * this. A BORROW out, "" (never NULL) until `set_dir` is first called. */
void rolltui_context_set_dir(RolltuiContext* ctx, const char* dir, size_t len);

/* a menu file the HOST carries in its own binary: `menu:<name>`'s middle rung. The text is copied in; the borrow out is valid until that
 * name is bound again or `w` is freed. `_count` / `_name_at` enumerate every host menu. */
void rolltui_context_add_menu(RolltuiContext* ctx, const char* name, size_t len, const char* json, size_t json_len);

/* what a `help` window shows: the lead as its first section ("About"), the scopes to list in order, and the note as its last
 * ("More"). `set_help` REPLACES lead and note; the scope list is built separately (`clear_help_scopes`, then `add_help_scope` per
 * entry), one call at a time. */
void rolltui_context_set_help(RolltuiContext* ctx, const char* lead, size_t lead_len, const char* note,
                              size_t note_len);

void rolltui_context_clear_help_scopes(RolltuiContext* ctx);

void rolltui_context_add_help_scope(RolltuiContext* ctx, const char* scope, size_t len);

void rolltui_context_set_env(RolltuiContext* ctx, const RolltuiWidgetEnv* env);

/* THE LIVE BINDINGS TABLE, as a handle, so a widget looks up an action's chords or asks whether a key is one of the transcript scope's.
 * A BORROW (`w` never frees it), set once per frame alongside `set_env`. NULL only before the first `set_env`. */
void rolltui_context_set_bindings(RolltuiContext* ctx, const RolltuiBindings* b);

/* ---- THE GAP REPORT: what this screen NAMES that this app does not PROVIDE ------------------
 * FOR THE DEVELOPER, not for whoever wrote the layout: the message is "the app was designed like this and your code does not support
 * it yet." A screen is the INTENT and the code catches up, so this REPORTS and never fails: it hands you a list and YOU decide
 * whether any of it is fatal. A layout may name a `browser` kind nobody has written yet, and that is a to-do, not an error.
 * CALL IT ONCE AT THE END OF INIT: after the screen is loaded, your kinds are registered and your sources bound, before the loop.
 * Not the same as `rolltui_windows_report_*`, which walks the layers currently PUSHED, per `sync`, as *problems this frame*. This
 * walks the WHOLE screen (the base and every popup the layout declares, opened or not) once, as *what you have not built*: a
 * `details` popup naming a kind you never wrote is invisible to `sync` until a user opens it. Each widget's own `problem()` answers
 * for itself.
 * TWO KINDS OF GAP, and the second is a HINT rather than a proof:
 *   - a thing that does not EXIST: a window names a kind nobody registered, or a source nothing is bound to. Exact.
 *   - a thing nothing can REACH: the screen declares an action and no chord serves it. A menu item may still invoke it, and whether
 *     your host HANDLES an action is not library-visible (handling is a `switch` in your own loop). Pass `b` NULL to skip this half.
 * `named` counts everything checkable that the screen names, so the summary can say "3 of 12". REPLACES `*out` (releasing whatever it
 * held), so one report may be reused. */
typedef struct RolltuiGapReport {
  RolltuiStr* gaps ROLLTUI_DEFAULT(nullptr); /* GROWING AMORTISED; one line per gap */
  size_t gaps_n ROLLTUI_DEFAULT(0), gaps_cap ROLLTUI_DEFAULT(0);
  size_t named ROLLTUI_DEFAULT(0); /* how many things the screen names that could be checked */
} RolltuiGapReport;

void rolltui_gap_report_release(RolltuiGapReport* r); /* frees everything; zeroes it */

int rolltui_gap_report_clean(const RolltuiGapReport* r); /* 1 when there is nothing to say */

/* "this screen names 12 things this app must provide and 3 are missing: <gap>; <gap>; <gap>".
 * "" when clean, which is the rule every report on this boundary has. APPENDS to `out`. */
void rolltui_gap_report_summary(const RolltuiGapReport* r, RolltuiStr* out);

/* `w` is not `const`: a kind's widget is BUILT to ask it, and building resolves a `menu:` file and a bound source. The instances are the
 * ones a later `sync` reuses, so this costs a screen's widgets once. */
void rolltui_gaps_collect(RolltuiWindows* w, const RolltuiLayout* l, const RolltuiBindings* b,
                          RolltuiGapReport* out);


/* The rect a widget draws in: the window's inner rect, less one column on each side when it
 * is bordered — the widget owns the breathing room inside the frame. */
void rolltui_content_rect(const RolltuiResolvedNode* rn, RolltuiRect* out);

/* The widget for `content`, created on demand and never destroyed until this `Windows` is (two windows on one content are two views of
 * one widget). */
RolltuiWidget* rolltui_windows_widget_for(RolltuiWindows* w, const char* content, size_t len);

/* THE CURRENT FRAME'S STYLE TABLE, indexed by Role ordinal: a BORROW valid for one `rolltui_windows_draw` call, set at its top from the
 * `styles` it is handed. It lets a widget's `draw` ask "what does Role::text look like" without the vtable's `draw` slot carrying a
 * parameter every kind must accept. NULL outside a draw call. */
const RolltuiStyle* rolltui_windows_styles(const RolltuiWindows* w);
/* ========================================================================================
 * diff — diff highlighting a host draws
 * ======================================================================================== */

/* One span of ONE line, as byte offsets into that line. `role` is a `rolltui::Role` value,
 * taken from the RolltuiDiffRoles the caller handed in and never invented here. */
typedef struct RolltuiDiffSpan {
  size_t begin, end; /* end exclusive */
  unsigned char role;
} RolltuiDiffSpan;

/* A line takes its whole role, or splits into line / word / line around its changed run: three is the maximum for any line, so a caller
 * sizes its buffer from this and never asks first. */
#define ROLLTUI_DIFF_MAX_SPANS 3

/* The seven roles this module may emit, handed in by the caller. `file_header` is the `---`/`+++` pair, which is NOT an added or removed
 * line; `hunk` is `@@`, a position rather than a change. */
typedef struct RolltuiDiffRoles {
  unsigned char added, removed, context, file_header, hunk, added_word, removed_word;
} RolltuiDiffRoles;

/* The block's lines, read on demand. Returns a BORROW of line `i`, valid for the duration
 * of the call; `*len` receives its length. A zero-length line gives a valid pointer. */
typedef const char* (*RolltuiDiffLineFn)(const void* block, size_t i, size_t* len);

/* ---- working memory ------------------------------------------------------------------ */
/* The decode, boundary and token-range buffers the word-level refinement needs, owned by the caller and reused: one handle per thread,
 * grown to a high-water mark over the first few calls. One buffer per ROLE, so the two sides of a pair cannot alias each other's token
 * ranges. It owns its own `RolltuiUnicodeScratch` (created lazily), which keeps this boundary at ONE handle. */
typedef struct RolltuiDiffScratch RolltuiDiffScratch;
/* THE DIFF ROLE MAPPING ITSELF, a BORROW of a static table. A caller that wants a DIFFERENT mapping still passes its own; this is the
 * default, not a replacement for the parameter. */
const RolltuiDiffRoles* rolltui_diff_default_roles(void);

RolltuiDiffScratch* rolltui_diff_scratch_new(void);

void rolltui_diff_scratch_free(RolltuiDiffScratch* s);

/* The spans of `block[index]`, into `out` (capacity `out_cap`, at least
 * ROLLTUI_DIFF_MAX_SPANS). Returns how many were written: 0 for a language this does not
 * claim and for an index past the block. */
size_t rolltui_diff_spans(RolltuiDiffScratch* s, const char* lang, size_t lang_len, const void* block,
                          size_t line_count, RolltuiDiffLineFn line_at, size_t index,
                          const RolltuiDiffRoles* roles, RolltuiDiffSpan* out, size_t out_cap);
/* ========================================================================================
 * ansi — another program's coloured output, read as styled lines
 * ======================================================================================== */

/* WHAT A PROGRAM WROTE TO A TERMINAL, as lines to draw: `bat`, `delta`, `ls --color`, a log, a man page. Text, newlines, tabs (to
 * stops every 8 columns), SGR (16, 256 and 24-bit colour, bold, dim, italic, underline, reverse, strikethrough) and backspace overstrike (`X\bX`
 * bold, `_\bX` underlined) are read; a carriage return not followed by a newline writes the line again, so a progress counter shows
 * its last state. OSC 8's hyperlinks are read onto the text they cover, from its opening to its closing, across lines as a terminal
 * keeps them, and drawn as links (a URL that is not printable ASCII, or an OSC past 4 KB, is none). Everything else a terminal would
 * act on (cursor moves, erase-in-display, every other OSC, DCS, APC) is swallowed: the output reads as what it says and never
 * commands this screen. A sequence or a character split across two feeds is joined. OWNED; the
 * lines are a ring capped at `rolltui_ansi_set_limit` (ROLLTUI_ANSI_DEFAULT_LIMIT), so a log keeps its newest. */
#define ROLLTUI_ANSI_DEFAULT_LIMIT 100000
RolltuiAnsiText* rolltui_ansi_new(void);
void rolltui_ansi_free(RolltuiAnsiText* t); /* a no-op on NULL */
void rolltui_ansi_feed(RolltuiAnsiText* t, const char* bytes, size_t len);
void rolltui_ansi_clear(RolltuiAnsiText* t);
/* 0: no cap. Lowering it drops the oldest lines at once. */
void rolltui_ansi_set_limit(RolltuiAnsiText* t, size_t max_lines);
/* The columns between tab stops another program's tabs reach (8 unless set; 1 to 16). */
void rolltui_ansi_set_tab_width(RolltuiAnsiText* t, int columns);
/* A STYLE FROM SGR's PARAMETERS ("1;38;5;208", "4:3;58:2::255:0:0"), as another program's output would set it: 1, or 0 when it is not
 * numbers 0 to 255 between ; and :. */
int rolltui_style_from_sgr(const char* params, size_t n, RolltuiStyle* out);
/* The lines so far; the one being written counts once it has text. */
size_t rolltui_ansi_line_count(const RolltuiAnsiText* t);
/* Line `i`'s text without its styles: a BORROW valid until the next feed. "" past the end. */
const char* rolltui_ansi_line(const RolltuiAnsiText* t, size_t i, size_t* len);
/* How many rows the text takes wrapped at `width`, and the widest line's columns: what a view scrolls over. */
size_t rolltui_ansi_rows(const RolltuiAnsiText* t, int width, int ambiguous_wide);
int rolltui_ansi_width(const RolltuiAnsiText* t);
/* DRAWS THE TEXT into `area`. `wrap` 1: lines wrap at the area's edge and `first` counts rows; 0: each line is one row, `first`
 * counts lines and `left` columns are scrolled off the left. A cell with no colour of its own takes `base`'s, so the program's
 * default colours are the theme's; the frame's depth maps the rest down at present. Returns the rows drawn. */
int rolltui_ansi_draw(const RolltuiAnsiText* t, RolltuiFrame* f, RolltuiRect area, size_t first, int left, int wrap,
                      RolltuiStyle base, int ambiguous_wide);
/* WHEN AND FROM WHERE each line came: lines begun after this carry `at_ms` (milliseconds since the epoch) and `tag` (a host's number for
 * the source, 0 none), as `rolltui_ansi_line_info` reads them back. */
void rolltui_ansi_set_stamp(RolltuiAnsiText* t, unsigned long long at_ms, unsigned char tag);
/* A RULE after what was written so far, `label` (may be NULL) on it: a line of its own, drawn as a rule across the width. */
void rolltui_ansi_mark(RolltuiAnsiText* t, const char* label, size_t len);
typedef struct RolltuiAnsiLineInfo {
  unsigned long long at_ms ROLLTUI_DEFAULT(0); /* when its first character came, by `set_stamp`; 0 unstamped */
  unsigned long long seq ROLLTUI_DEFAULT(0);   /* its number since the text was made: dropping old lines never renumbers one */
  unsigned char tag ROLLTUI_DEFAULT(0);
  unsigned char rule ROLLTUI_DEFAULT(0); /* a rule `rolltui_ansi_mark` made; its text is the label */
} RolltuiAnsiLineInfo;
/* Line `i`'s stamp, number and kind: 0 past the end. */
int rolltui_ansi_line_info(const RolltuiAnsiText* t, size_t i, RolltuiAnsiLineInfo* out);
/* The rows line `i` takes wrapped at `width`. */
size_t rolltui_ansi_line_rows(const RolltuiAnsiText* t, size_t i, int width, int ambiguous_wide);
/* ONE LINE DRAWN into `area`, from its row `skip` when wrapped or its column `left` when not; the bytes in `lit` (`lit_n` pairs of
 * [begin, end), BORROWED for the call) patched with `lit_style` over their own, as a filter lights its matches. A rule is drawn across
 * the width. Returns the rows drawn. */
typedef struct RolltuiAnsiLineDraw {
  int skip ROLLTUI_DEFAULT(0);
  int left ROLLTUI_DEFAULT(0);
  unsigned char wrap ROLLTUI_DEFAULT(1);
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  RolltuiStyle base;
  const size_t* lit ROLLTUI_DEFAULT(ROLLTUI_NULL);
  size_t lit_n ROLLTUI_DEFAULT(0);
  RolltuiStyle lit_style;
} RolltuiAnsiLineDraw;
int rolltui_ansi_draw_line(const RolltuiAnsiText* t, size_t i, RolltuiFrame* f, RolltuiRect area, const RolltuiAnsiLineDraw* o);

/* ========================================================================================
 * list — a list, a table or a tree the host feeds by index
 * ======================================================================================== */

/* A `list:<name>` WINDOW over data the host holds. The list asks how many rows there are and, for row `i`, what it says; a plain
 * list asks only for the rows it draws, so a hundred thousand rows cost a screenful. Sorting, a filter or a tree need every row
 * once, when the data or the question changes (`rolltui_windows_list_refresh`, or the count moving), never per frame.
 *
 * A ROW IS ITS KEY: the cursor, the marks and which nodes are open follow a row through a refresh, a sort or a filter, and the
 * cursor keeps its place on screen when its row moved. A row with no key is keyed by its index. */
#define ROLLTUI_LIST_SPAN_MAX 16
typedef struct RolltuiListSpan {
  unsigned char cell ROLLTUI_DEFAULT(0), role ROLLTUI_DEFAULT(0);
  unsigned short begin ROLLTUI_DEFAULT(0), end ROLLTUI_DEFAULT(0); /* bytes of that cell; end exclusive */
} RolltuiListSpan;
typedef struct RolltuiListRow {
  RolltuiStr key;
  RolltuiStrListRaw cells;                /* one per column; the first is the row's own text (`rolltui_list_row_cell` adds one) */
  unsigned char role ROLLTUI_DEFAULT(0);  /* a role for the whole row (an added file, an error); 0: text */
  unsigned char depth ROLLTUI_DEFAULT(0); /* a tree's: 0 at the top; rows come in tree order, a parent before its children */
  unsigned char busy ROLLTUI_DEFAULT(0);  /* what it is doing is running: a spinner at the row's end, from `busy_since_ms` */
  unsigned long long busy_since_ms ROLLTUI_DEFAULT(0);
  /* CELLS OF THEIR OWN. Bit k of `styled`: cell k is drawn in role `cell_role[k]` on every row, the cursor's included (a theme's
   * sample of that role). Bit k of `swatch`: cell k is a colour's spelling, drawn after a swatch of that colour. */
  unsigned char styled ROLLTUI_DEFAULT(0), swatch ROLLTUI_DEFAULT(0);
  unsigned char cell_role[8];
  /* SPANS: parts of a cell drawn in a role of their own over the row's (`rolltui_list_row_span`): a permissions string a letter at a
   * time, a word the host found. Up to ROLLTUI_LIST_SPAN_MAX a row. */
  RolltuiListSpan spans[ROLLTUI_LIST_SPAN_MAX];
  unsigned char span_n ROLLTUI_DEFAULT(0);
  /* A MARK OF THE ROW'S OWN in the gutter (with `gutter` or `marks` on): one glyph, copied, cut or tagged, in `gutter_role`; "" none.
   * It stands in place of the marks' ▌. */
  char gutter[8];
  unsigned char gutter_role ROLLTUI_DEFAULT(0);
} RolltuiListRow;
/* Adds a span to `r`: bytes `begin` to `end` of cell `cell`, in `role`. A span past ROLLTUI_LIST_SPAN_MAX is not kept. */
void rolltui_list_row_span(RolltuiListRow* r, size_t cell, size_t begin, size_t end, unsigned char role);
/* Appends one cell to `r`, copied. */
void rolltui_list_row_cell(RolltuiListRow* r, const char* text, size_t len);
typedef size_t (*RolltuiListCountFn)(void* ctx);
/* Fills `out` (cleared) with row `i` of the host's data, in the host's own order. */
typedef void (*RolltuiListRowFn)(void* ctx, size_t i, RolltuiListRow* out);

#define ROLLTUI_LIST_SORT_TEXT 0   /* natural order (`rolltui_natural_compare`): file2 before file10, case and accents aside */
#define ROLLTUI_LIST_SORT_NUMBER 1 /* by the number a cell starts with, a size's K, M, G, T and an age's m, h, d read; text after */
#define ROLLTUI_LIST_ALIGN_LEFT 0
#define ROLLTUI_LIST_ALIGN_RIGHT 1
/* A TABLE'S COLUMN. A row with fewer cells than the table has columns draws its last cell across the columns it has none for.
 * The first takes the width the others leave; the rest are `width` cells, or as wide as the widest cell they
 * have shown (a measured column only grows, so it does not jitter as the list scrolls), up to `max_share` percent of the list's
 * width (0: no limit) so the first keeps the rest. `min_screen`: shown only while the list is at least this wide, so a narrow
 * screen drops the column. */
typedef struct RolltuiListColumn {
  RolltuiStr title;
  int width ROLLTUI_DEFAULT(0);
  unsigned char align ROLLTUI_DEFAULT(ROLLTUI_LIST_ALIGN_LEFT);
  int min_screen ROLLTUI_DEFAULT(0);
  unsigned char sort ROLLTUI_DEFAULT(ROLLTUI_LIST_SORT_TEXT);
  /* The role this column's cells are drawn in on a plain row: a column of keys in the shortcut role, as keys are drawn everywhere.
   * 0: the row's. A row with a role of its own, the cursor's and the hovered row keep theirs. */
  unsigned char role ROLLTUI_DEFAULT(0);
  unsigned char max_share ROLLTUI_DEFAULT(0);
  /* Kept however narrow the list: the first column is cut rather than this one dropped (a sheet's keys are what it is for). */
  unsigned char keep ROLLTUI_DEFAULT(0);
} RolltuiListColumn;

typedef struct RolltuiListOptions {
  /* A TREE: rows nest by `depth`, guide lines draw the shape, a node opens and closes, and a filter keeps (and opens) the parents of
   * what it matches. Nodes shallower than `open_depth` start open, the rest closed (0: every node). Enter opens or closes a node
   * unless `activate_nodes`. */
  unsigned char tree ROLLTUI_DEFAULT(0);
  unsigned char open_depth ROLLTUI_DEFAULT(0);
  unsigned char activate_nodes ROLLTUI_DEFAULT(0);
  /* MARKS: several rows chosen at once, by key: a mark column, Space, a range with Shift and the arrows or a drag, all, none and
   * the inverse. Back clears them. */
  unsigned char marks ROLLTUI_DEFAULT(0);
  unsigned char line_numbers ROLLTUI_DEFAULT(0); /* 1: each row's number; 2: relative to the cursor's */
  int scrolloff ROLLTUI_DEFAULT(0);              /* rows kept between the cursor and the edge; past half the view, centred */
  unsigned char wrap ROLLTUI_DEFAULT(0);         /* the cursor goes from the last row to the first, and back */
  unsigned char hide_header ROLLTUI_DEFAULT(0);  /* no row of the columns' titles (the sorted one marked ▲ or ▼) */
  /* GROUPED UNDER HEADINGS, with `tree` set: a row at depth 0 is a section heading (bold, a rule to the edge), its rows at depth 1
   * under it. The cursor passes over headings, every group stays open, no guide lines are drawn, a filter keeps a heading while a
   * row under it matches, and the footer counts the rows, not the headings. */
  unsigned char headings ROLLTUI_DEFAULT(0);
  /* A GUTTER for rows' own marks (`RolltuiListRow.gutter`), shown even with `marks` off. */
  unsigned char gutter ROLLTUI_DEFAULT(0);
  /* FADED BY DISTANCE: each row this many percent fainter for every row it is from the cursor, to at most 70 (0: none). */
  unsigned char fade ROLLTUI_DEFAULT(0);
  /* A COLUMN CURSOR in a table (not a tree): Left and Right move it, its title is drawn reversed and its cell on the cursor's row
   * underlined, and `list.sort` sorts by it, then the other way, then the host's order. */
  unsigned char column_cursor ROLLTUI_DEFAULT(0);
  /* A NODE WITH MORE CHILDREN than this starts closed, whatever `open_depth` says (0: no such limit): a folder of thousands stays
   * shut until it is asked for. */
  unsigned int close_over ROLLTUI_DEFAULT(0);
  /* ROWS MOVED BY THE PERSON: Alt-Shift-Up and Alt-Shift-Down, or a drag that starts on a row (on a marked one, with `marks`). The list
   * reports a `REORDER` event and the host moves its rows; while the list is sorted or a tree, nothing moves. */
  unsigned char reorder ROLLTUI_DEFAULT(0);
  /* CHANGED ROWS: a row that appeared or changed at a refresh is drawn bold, italic and in the `info` role for this many milliseconds
   * (0: off). Without a frame clock, until the next refresh. */
  unsigned short changes_ms ROLLTUI_DEFAULT(0);
} RolltuiListOptions;

/* WHAT HAPPENED, popped one at a time: the cursor moved to `key`, `key` was activated (Enter, a double click), the marks changed,
 * the sort changed, `key` opened or closed, the filter is now `key`. */
#define ROLLTUI_LIST_EVENT_MOVED 1
#define ROLLTUI_LIST_EVENT_ACTIVATE 2
#define ROLLTUI_LIST_EVENT_MARKED 3
#define ROLLTUI_LIST_EVENT_SORTED 4
#define ROLLTUI_LIST_EVENT_OPENED 5
#define ROLLTUI_LIST_EVENT_CLOSED 6
#define ROLLTUI_LIST_EVENT_FILTERED 7
/* `key` moved to go just before `before` ("" : to the end). With `marks`, a moved row that is marked moves every marked row. */
#define ROLLTUI_LIST_EVENT_REORDER 8
typedef struct RolltuiListEvent {
  unsigned char kind;
  RolltuiStr key;
  RolltuiStr before; /* REORDER only */
} RolltuiListEvent;
void rolltui_list_event_release(RolltuiListEvent* e);

/* The data a `list:<name>` window shows: `count(ctx)` rows, row `i` filled by `row(ctx, i, out)`. Binding again replaces the source;
 * `free_ctx` (may be NULL) is called on `ctx` when it is replaced and when `w` is freed. */
void rolltui_windows_bind_list(RolltuiWindows* w, const char* name, size_t len, RolltuiListCountFn count, RolltuiListRowFn row,
                               void* ctx, void (*free_ctx)(void*));
void rolltui_windows_set_list_options(RolltuiWindows* w, const char* name, size_t len, const RolltuiListOptions* o);
/* The columns, copied; none: each row is its first cell across the whole width. */
void rolltui_windows_set_list_columns(RolltuiWindows* w, const char* name, size_t len, const RolltuiListColumn* cols, size_t n);
/* The data changed: the rows are asked for again before the next draw. */
void rolltui_windows_list_refresh(RolltuiWindows* w, const char* name, size_t len);
void rolltui_windows_list_set_filter(RolltuiWindows* w, const char* name, size_t len, const char* query, size_t qlen);
void rolltui_windows_list_set_cursor(RolltuiWindows* w, const char* name, size_t len, const char* key, size_t klen);
/* 1 with the cursor's row's key in `*key` (may be NULL); 0 when the list shows nothing. */
int rolltui_windows_list_cursor(RolltuiWindows* w, const char* name, size_t len, RolltuiStr* key);
/* How many rows are marked; their keys REPLACE `*keys` (may be NULL). */
size_t rolltui_windows_list_marked(RolltuiWindows* w, const char* name, size_t len, RolltuiStrListRaw* keys);
/* 1 with the next event in `*out` (its key replaced); 0 when there is none. */
int rolltui_windows_list_event(RolltuiWindows* w, const char* name, size_t len, RolltuiListEvent* out);
/* Does what a key bound to `action` (a `list.*` name, as a bindings file spells it) does: a host's own menu entry or command for
 * "mark all". `filter` starts typing in the list's last row: each space-separated term must match within one cell, fuzzily
 * (`rolltui_fuzzy_match`), `'term` as written, `!term` not at all; Enter keeps it. `back` clears the filter, then the marks. 1 when it
 * did something. */
int rolltui_windows_list_perform(RolltuiWindows* w, const char* name, size_t len, const char* action, size_t alen);
/* THE SORT, set and read by a host: by `column` (-1, the host's own order), `reverse` the other way; `_sort_state` answers 1 while
 * the list is sorted. A `SORTED` event says it changed; this says how. */
void rolltui_windows_list_sort(RolltuiWindows* w, const char* name, size_t len, int column, int reverse);
int rolltui_windows_list_sort_state(RolltuiWindows* w, const char* name, size_t len, int* column, int* reverse);
/* The column a table's column cursor is on (`column_cursor`): the cell a host copies, the one it edits. */
size_t rolltui_windows_list_cursor_column(RolltuiWindows* w, const char* name, size_t len);
/* MARKS BY PATTERN: every row a filter query matches (the list's own terms, `rolltui_filter_new`), shown or not, marked (`on`) or
 * unmarked. How many rows it touched. */
size_t rolltui_windows_list_mark_matching(RolltuiWindows* w, const char* name, size_t len, const char* query, size_t qlen, int on);
/* REVEAL a row by its key: in a tree its parents are opened, and the cursor goes to it. 0 when no row has the key. */
int rolltui_windows_list_reveal(RolltuiWindows* w, const char* name, size_t len, const char* key, size_t klen);
/* A tree's node opened (`open` 1) or closed by the host, as the person's key would. 0 when no node has the key. */
int rolltui_windows_list_set_open(RolltuiWindows* w, const char* name, size_t len, const char* key, size_t klen, int open);
/* A VIEW PUSHED INTO A WINDOW: `content` ("list:procs", "text:...") drawn in its place with its own keys, until popped; the window's
 * own content keeps its state underneath. The title reads the views' titles after its own ("files › details"). Escape pops one once
 * the view has nothing of its own to back out of. 0 when no window has that id. */
int rolltui_windows_push_view(RolltuiWindows* w, const char* window, size_t len, const char* content, size_t clen, const char* title,
                              size_t tlen);
int rolltui_windows_pop_view(RolltuiWindows* w, const char* window, size_t len);
size_t rolltui_windows_view_depth(const RolltuiWindows* w, const char* window, size_t len);
/* THE ROWS ARE STILL COMING: an empty list says "loading" (after 100 ms, with a spinner) instead of "no rows", and the footer says
 * so while rows arrive. 0 when they have all come. */
void rolltui_windows_list_set_loading(RolltuiWindows* w, const char* name, size_t len, int loading);
/* PAUSED: the list keeps the rows it has while the host's change underneath it, and its footer says "paused"; 0 takes the host's
 * rows again. The other windows keep updating. */
void rolltui_windows_list_freeze(RolltuiWindows* w, const char* name, size_t len, int frozen);

/* ---- ordering and finding text ---------------------------------------------------------------- */
/* NATURAL ORDER: a run of digits against a run of digits by value (`file2` before `file10`, `07` beside `7`), the rest by its
 * case-folded, accent-stripped code points, the bytes as written breaking a tie. <0, 0, >0. */
int rolltui_natural_compare(const char* a, size_t alen, const char* b, size_t blen);
/* FUZZY: whether `pattern`'s letters appear in `text` in order, case and accents aside unless the pattern has an uppercase letter
 * (smart case). `*score` (may be NULL) is higher for a better match: a run of letters, a match at a word's start (after a space,
 * a separator or a lower-to-upper change), early in the text. `positions` (may be NULL) gets each matched letter's byte offset in
 * `text`, up to `cap`. An empty pattern matches everything. */
int rolltui_fuzzy_match(const char* pattern, size_t pattern_len, const char* text, size_t text_len, int* score, size_t* positions,
                        size_t cap);
/* FIND AS A PERSON MEANS IT: `needle` in `hay` from byte `from`, case and accents aside unless the needle has an uppercase letter
 * (smart case). 1 with the match's byte range in `*at` and `*len` (either may be NULL), else 0. */
int rolltui_text_find(const char* hay, size_t hay_len, const char* needle, size_t needle_len, size_t from, size_t* at, size_t* len);
/* Every place `needle` is in `hay`, the same way, in order and not overlapping: `fn` with each match's byte range. How many. */
size_t rolltui_text_find_each(const char* hay, size_t hay_len, const char* needle, size_t needle_len,
                              void (*fn)(void* ctx, size_t at, size_t len), void* ctx);

/* THE FILTER every list, menu and palette reads, for a host's own rows too: terms apart by spaces, each of which must match. A plain
 * term matches fuzzily (`rolltui_fuzzy_match`), `'term` as written, `^term` at the start, `term$` at the end, `/re/` as a regular
 * expression, and `!term` must not appear; case and accents aside unless a term has an uppercase letter. Made once per query. */
typedef struct RolltuiFilter RolltuiFilter;
RolltuiFilter* rolltui_filter_new(const char* query, size_t len);
void rolltui_filter_free(RolltuiFilter* f); /* a no-op on NULL */
/* Whether every term matches `text`, and how well (`*score`, higher is better; may be NULL). A NULL filter matches everything. */
int rolltui_filter_match(const RolltuiFilter* f, const char* text, size_t len, int* score);
/* One term at a time, for rows of several cells where a term must match within one of them: how many terms, whether term `i`
 * matches `text` (as written: a `!` term's answer is whether the text it names is there), and whether it is a `!` term. */
size_t rolltui_filter_terms(const RolltuiFilter* f);
int rolltui_filter_term(const RolltuiFilter* f, size_t i, const char* text, size_t len, int* score);
int rolltui_filter_term_negated(const RolltuiFilter* f, size_t i);
/* WHAT MATCHED, to light it: byte ranges of `text` the filter's terms matched, `begin[k]` to `end[k]`, up to `cap`. How many. */
size_t rolltui_filter_spans(const RolltuiFilter* f, const char* text, size_t len, size_t* begin, size_t* end, size_t cap);

/* ========================================================================================
 * charts — numbers drawn: a line, bars, a sparkline
 * ======================================================================================== */

/* A CHART of up to four series. A LINE is drawn in braille dots, two across and four down a cell, its points joined; BARS stand on
 * zero, a group a value and a bar a series in it; a SPARKLINE is one row a series, a block a value. More values than fit shows the
 * newest. The range is the values' own (bars and sparklines from zero) unless set. A NaN value is a gap. The range's labels are on
 * the left and the series' names, each in its colour and bold, on the top row. Braille is narrow in every terminal; where ambiguous
 * glyphs are two cells wide the blocks are ASCII. */
#define ROLLTUI_CHART_LINE 0
#define ROLLTUI_CHART_BARS 1
#define ROLLTUI_CHART_SPARK 2
/* AN AREA: the line filled to its foot. A row covers a band of values: full above the value, empty below it, the row the value is in
 * filled to it; any value above the least shows at least one dot. */
#define ROLLTUI_CHART_AREA 3
/* A METER a series: its newest value as a bar of ■ along the row, the rest of the row a track in the axis role. */
#define ROLLTUI_CHART_METER 4
/* BY THE ROOM: a meter on one row, a sparkline on two or three, a line on more. */
#define ROLLTUI_CHART_AUTO 5
#define ROLLTUI_CHART_SERIES_MAX 4
typedef struct RolltuiChart RolltuiChart;
RolltuiChart* rolltui_chart_new(void);
void rolltui_chart_free(RolltuiChart* c); /* a no-op on NULL */
/* Back to a line with no series and its own range: what a fill function is handed. */
void rolltui_chart_clear(RolltuiChart* c);
void rolltui_chart_set_kind(RolltuiChart* c, unsigned char kind);
/* Series `i` (0-3): its name (may be empty), its role (0: the accents in order) and its values, copied. 0 for an `i` past the last. */
int rolltui_chart_set_series(RolltuiChart* c, size_t i, const char* label, size_t label_len, unsigned char role, const double* values,
                             size_t n);
/* The range the chart spans, fixed: a percentage is 0 to 100 however high it has been. Ignored unless `hi` > `lo`. */
void rolltui_chart_set_range(RolltuiChart* c, double lo, double hi);
/* THE SYMBOLS a line or an area is drawn in: braille (two dots across a cell, four down), quadrant blocks (two by two), or shades
 * (one a cell, ░▒▓█ by how full). DEFAULT is the theme's (`glyphs.chart`: "braille", "blocks", "shades"), braille when it says
 * nothing. The Linux console draws braille as shades. */
#define ROLLTUI_CHART_SYMBOLS_DEFAULT 0
#define ROLLTUI_CHART_SYMBOLS_BRAILLE 1
#define ROLLTUI_CHART_SYMBOLS_BLOCKS 2
#define ROLLTUI_CHART_SYMBOLS_SHADES 3
void rolltui_chart_set_symbols(RolltuiChart* c, unsigned char symbols);
/* WAYS A CHART IS DRAWN, OR'd. FLOW: a time series, a value a column (a dot across in a line or area, a cell in bars and a sparkline),
 * the newest at the right and the left empty while there are fewer than fit, never stretched. AXES: a line up the plot's left and
 * one along its foot, in box drawing. INVERT: the chart hangs from its top, the least there and the most at the foot, its names on
 * the bottom row; a meter fills from its right. MIRROR: the first two series as a pair, the first standing on the middle and the
 * second hanging under it, each with its own range (one series is drawn as usual; give them in the other order to swap them). MID: a
 * rule between the pair. */
#define ROLLTUI_CHART_FLOW 1
#define ROLLTUI_CHART_AXES 2
#define ROLLTUI_CHART_INVERT 4
#define ROLLTUI_CHART_MIRROR 8
#define ROLLTUI_CHART_MID 16
void rolltui_chart_set_flags(RolltuiChart* c, unsigned flags);
/* How many values a series shows in `cells` columns at this chart's kind and symbols: the most a history for it needs to keep. */
size_t rolltui_chart_capacity(const RolltuiChart* c, int cells);
/* TIME ALONG THE FOOT, with AXES: the newest value is at `newest_s` (seconds since the epoch), each one `step_s` after the one before;
 * HH:MM labels under the axis every ten columns from the right. */
void rolltui_chart_set_time(RolltuiChart* c, double newest_s, double step_s);
/* A GRADIENT: a colour from `start` at the least to `end` at the most, through `mid` when it has one. `end` none: `start` alone; `mid`
 * none: two stops. Only RGB stops are blended; a palette stop is taken as it is. */
typedef struct RolltuiGradient {
  RolltuiStyleColorRaw start, mid, end;
} RolltuiGradient;
/* The colour at `t` (0 to 1), into `*out`: a number coloured by where it falls. */
void rolltui_gradient_at(const RolltuiGradient* g, double t, RolltuiStyleColorRaw* out);
/* A THEME'S GRADIENT by name, from its `"gradients": { "cpu": { "start": "#2a7", "mid": "yellow", "end": "#d33" } }`, each stop any
 * colour a role takes (a `defs` name, a dark and light pair). `root` is the theme's colours, as `rolltui_theme_load` reads them;
 * `mode` below zero follows the terminal. 1, or 0 when the theme has none by that name. */
int rolltui_theme_gradient(const RolltuiJsonValue* root, int mode, const char* name, size_t len, RolltuiGradient* out);
/* Series `i` in a gradient (NULL: its role's colour): a line or an area coloured by height a row at a time, a sparkline and bars a
 * value at a time, a meter along its length. */
void rolltui_chart_set_gradient(RolltuiChart* c, size_t i, const RolltuiGradient* g);
/* A KEPT HISTORY for a chart that flows: the newest `cap` values pushed. OWNED. */
typedef struct RolltuiHistory RolltuiHistory;
RolltuiHistory* rolltui_history_new(size_t cap);
void rolltui_history_free(RolltuiHistory* h); /* a no-op on NULL */
void rolltui_history_push(RolltuiHistory* h, double v);
/* Keeps at most `cap`, the newest: what `rolltui_chart_capacity` says the chart shows. */
void rolltui_history_resize(RolltuiHistory* h, size_t cap);
size_t rolltui_history_count(const RolltuiHistory* h);
/* Series `i` from a history, oldest first: as `rolltui_chart_set_series` with its values. */
int rolltui_chart_set_series_history(RolltuiChart* c, size_t i, const char* label, size_t label_len, unsigned char role,
                                     const RolltuiHistory* h);
/* Draws `c` into `area` in the table's roles (`styles` indexed by role): a host's own widget, a dashboard cell. */
void rolltui_chart_draw(RolltuiChart* c, RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect area, const RolltuiStyle* styles,
                        int ambiguous_wide);
/* A SPARKLINE AS TEXT, one cell a value: ▁ to █ from `lo` to `hi` (the values' own range when `lo` >= `hi`), a NaN a space. With
 * `ascii`, where block elements are wide, `_.-~^`. Appended to `out`; a list's cell, a status line. */
void rolltui_sparkline_text(const double* values, size_t n, double lo, double hi, int ascii, RolltuiStr* out);
/* A `chart:<name>` window's numbers: `fn` fills the chart it is handed (cleared first) before each frame. */
typedef void (*RolltuiChartFn)(void* ctx, RolltuiChart* out);
void rolltui_windows_bind_chart(RolltuiWindows* w, const char* name, size_t len, RolltuiChartFn fn, void* ctx, void (*free_ctx)(void*));

/* ========================================================================================
 * images — pictures, fitted to a window and drawn the best way the terminal has
 * ======================================================================================== */

/* HOW A TERMINAL DRAWS PICTURES, chosen when it is entered (`RolltuiTermFacts::images`):
 *   BLOCKS  half blocks, two pixels to a cell in their colours: every terminal, every multiplexer, ssh.
 *   KITTY   the kitty graphics protocol's Unicode placeholders (kitty, Ghostty): the picture is IN the cells, so a popup over it,
 *           the diff and tmux treat it as text, and its pixels are sent once.
 *   ITERM2  OSC 1337 (iTerm2, WezTerm): drawn by the terminal at a cell, outside the grid.
 *   SIXEL   (foot, xterm -ti 340, mlterm, Konsole, Windows Terminal; any terminal whose Primary DA names sixel): likewise.
 * An outside-the-grid picture is sent whenever its cells are all its own blanks and were not last frame, and never while anything is
 * drawn over it; when it goes, its cells are written again, erasing it. `ROLLTUI_IMAGES` (blocks, kitty, iterm2, sixel) chooses
 * outright. 0 is "not known": no terminal is entered, and pictures are drawn in blocks. */
#define ROLLTUI_IMAGES_BLOCKS 1
#define ROLLTUI_IMAGES_KITTY 2
#define ROLLTUI_IMAGES_ITERM2 3
#define ROLLTUI_IMAGES_SIXEL 4
/* A picture larger than this on a side, or than this many bytes decoded, is refused before it is decoded. */
#define ROLLTUI_IMAGE_MAX_SIDE 10000
#define ROLLTUI_IMAGE_MAX_BYTES (512u * 1024u * 1024u)

/* A PICTURE, decoded to RGBA: PNG, JPEG, GIF (its first frame), BMP, TGA and PNM. OWNED. NULL with the reason in `*err` (may be
 * NULL): "not a picture this library reads", "too large to decode: 12000x9000". */
RolltuiImage* rolltui_image_load(const char* path, size_t len, RolltuiStr* err);
RolltuiImage* rolltui_image_decode(const unsigned char* bytes, size_t len, RolltuiStr* err);
/* `w` x `h` RGBA pixels, copied. NULL when the size is not one a picture may be. */
RolltuiImage* rolltui_image_from_rgba(const unsigned char* rgba, int w, int h);
void rolltui_image_free(RolltuiImage* img);
/* HOW A PICTURE IS SCALED to its cells: SMOOTH (the default) averages the pixels each one covers; NEAREST takes the pixel under each
 * one's centre, so pixel art and a screenshot of text stay hard-edged. */
#define ROLLTUI_IMAGE_SMOOTH 0
#define ROLLTUI_IMAGE_NEAREST 1
void rolltui_image_set_filter(RolltuiImage* img, int filter); /* a no-op on NULL */
/* The pixels, RGBA, row by row: a BORROW valid until the picture is freed. */
const unsigned char* rolltui_image_rgba(const RolltuiImage* img, int* w, int* h);
/* DRAWS `img` into `area`, its aspect kept and never enlarged past its own pixels, at the area's top left, by the terminal's way
 * (BLOCKS while none is entered). `ground` is what shows through a transparent pixel and what an outside-the-grid picture's cells
 * hold. `*used` (may be NULL) is the cells it took. `img` is a BORROW the host keeps alive until the frame has been presented. */
void rolltui_frame_image(RolltuiFrame* f, RolltuiRect area, const RolltuiImage* img, RolltuiStyle ground, RolltuiRect* used);
/* The cells `rolltui_frame_image` would take for `img` in a `cols` x `rows` area, now: what a host centres a picture with. */
void rolltui_image_cells(const RolltuiImage* img, int cols, int rows, int* used_cols, int* used_rows);
/* A PICTURE AN OUTSIDE PROGRAM DRAWS (`kitten icat --place`, chafa's sixel, a window over the terminal such as Überzug++): `cells`,
 * clipped to the frame, are held blank in `ground` under the host's `id` (not 0), and the terminal tells the host when to draw the
 * picture and when to clear it (`rolltui_terminal_set_outside_pictures`). */
void rolltui_frame_outside_picture(RolltuiFrame* f, unsigned int id, RolltuiRect cells, RolltuiStyle ground);

/* ========================================================================================
 * marker — the "N more" rule
 * ======================================================================================== */

/* It SHORTENS rather than eating the line: the marker writes over CONTENT cells, and at 20 cells wide the full form took most of the
 * row. That is safe because the scrollbar carries the proportion; the bar is the positional signal and the marker the NON-GRAPHICAL one,
 * which a mono theme, a low colour depth or a borderless window still has. Writes 0 bytes when there is nothing below or no room.
 * `out` needs ROLLTUI_MARKER_MAX. */
#define ROLLTUI_MARKER_MAX 40
size_t rolltui_scroll_marker_text(size_t below, int max_width, int ambiguous_wide, char* out, size_t cap);
/* ========================================================================================
 * widget_kinds — the built-in kinds and their registration
 * ======================================================================================== */

/* THE ROLE BYTES these kinds draw with, handed over ONCE at registration: this file names no role. Role ordinals are process-wide
 * constants, so a value copied in at construction never goes stale. */
typedef struct RolltuiBuiltinRoles {
  unsigned char text, text_muted, error, scroll_marker, label, value;
  unsigned char input_text, input_selection, input_placeholder;
  unsigned char hover; /* what a click at the pointer would act on (`RolltuiResolvedNode::hover`) */
  /* a gauge's bar, a toast's ground and border, and the three message levels that are not `error` */
  unsigned char gauge, panel, border, info, ok, warning;
  /* a list's cursor, focused and not, the mark on a marked row, and the filter's prompt */
  unsigned char cursor, cursor_away, mark, prompt;
  /* a chart's series, when one names no role of its own */
  unsigned char series[ROLLTUI_CHART_SERIES_MAX];
  /* a legend drawn in the focus colours while its window has the focus: a count's */
  unsigned char focus;
} RolltuiBuiltinRoles;

/* THE SIX ACTION NAMES the transcript SCOPE's scroll keys use: this file knows the rule and none of the words (the same trade
 * `RolltuiTranscriptActions` makes); an action name does not cross the C boundary. */
typedef struct RolltuiScrollTextActions {
  const char *line_up, *line_down, *page_up, *page_down, *top, *bottom;
} RolltuiScrollTextActions;

/* The two ints a transcript's code-block folding needs, mirrored to the boundary so the transcript kind can read them at layout time. */
typedef struct RolltuiCodeFold {
  int fold_over_lines, cap_lines;
} RolltuiCodeFold;
/* The LIBRARY'S OWN six — the same list `RolltuiTranscriptActions` draws from,
 * minus the five that need a transcript. BORROWS static storage. */
const RolltuiScrollTextActions* rolltui_scroll_text_default_actions(void);

/* THE FIVE VOCABULARIES AND THE KINDS, IN ONE CALL: every setter in this file with the library's own defaults, then
 * `rolltui_widget_kinds_register`, in the order that function requires. It makes a bare `rolltui_windows_new()` usable by a pure-C host.
 * A host with its own words calls the setters after; this is a default, not a policy. Idempotent; NULL is a no-op. */
void rolltui_context_set_library_defaults(RolltuiContext* ctx);

/* line_up/down, page_up/down, top/bottom applied to a `page`-row view of `total` lines, with
 * `*top` clamped to [0, total-page]. 0 when the chord is not one of the six. */
int rolltui_scroll_by_action(const RolltuiScrollTextActions* actions, const RolltuiBindings* bindings,
                             const RolltuiChord* k, int page, int total, int* top);
void rolltui_context_set_code_fold(RolltuiContext* ctx, const RolltuiCodeFold* c);
/* Resolves a kind NAME. `*row` is its row in the one enumeration, filled for BOTH answering
 * rungs; `rule` and `source_is` (a BORROW valid until the registry changes) likewise. Any
 * out-param may be NULL. */
int rolltui_widget_kind_resolve(const RolltuiContext* c, const char* name, size_t len, size_t* row,
                                unsigned char* rule, const char** source_is, size_t* source_is_len);

/* The one enumeration. Rows [0, library_count) are the library's closed table, in table order;
 * rows [library_count, count) are a host's, in registration order. A row past the end reads as
 * "" / REQUIRED / NAME rather than past either table. */
/* FOR THE APP AUTHOR: the NAMES in this registry are what a LAYOUT FILE writes as a window's `content`: `transcript`, `input:prompt`,
 * `rows:status`, and any kind a host registered (the explorer's `browser`, paint's `canvas`). A name no kind answers to is a NAMED
 * problem and a visible error panel, never a blank window, so a layout may name a kind a host has not written yet and be told so.
 * A layout is shipped by the app (see PART 1's opening): a host BINDS these sources and NAMES these windows in its own code, so a
 * renamed or dropped window breaks the app silently. Shipped files: `rolltui/presets/layouts/` and `rolltui/presets/menus/`; the example
 * apps carry their own under `rolltui/examples/presets/`. */
size_t rolltui_widget_kind_count(const RolltuiContext* c);

size_t rolltui_widget_kind_library_count(void);

unsigned char rolltui_widget_kind_source_shape(size_t row);

int rolltui_widget_kind_register(RolltuiContext* c, const char* name, size_t len, unsigned char rule,
                                 const char* source_is, size_t source_is_len);

/* ========================================================================================
 * window_stack — a runtime stack of windows built from a resolved layout, and its events
 * ======================================================================================== */

/* A stack with one empty base layer, which is what `WindowStack{}` has always meant. */
RolltuiWindowStack* rolltui_window_stack_new(void);

void rolltui_window_stack_free(RolltuiWindowStack* s);

/* Replaces the base layer by COPY. Popup layers stay; the base's focus id is kept when a
 * window with that id still exists. */
void rolltui_window_stack_set_base(RolltuiWindowStack* s, const RolltuiLayer* base);
/* A host hands the stack the window table's `rolltui_windows_back` once, and the stack asks it, by the focused window's id, before it pops
 * a popup: the close key and a press outside a dismissing popup close one level at a time. Without it a close is the popup's, whole. */
void rolltui_window_stack_set_level_fn(RolltuiWindowStack* s, RolltuiStackLevelFn fn, void* ctx);

/* PUSHES A POPUP THE LAYOUT DECLARED, BY ID: deep-copies it and pushes the copy. 1 when the layout declares one of that id, 0 when it
 * does not (nothing is pushed). A layer a HOST built itself still goes through `_push`. This exists because
 * `RolltuiLayer copy = *p; rolltui_window_stack_push(stack, &copy);` is a different operation in the two languages: under C++ it is a
 * DEEP copy through `rolltui_layer_copy`, in C a shallow struct assignment whose copy aliases the layout's own buffers, so freeing the
 * stack double-frees (measured by `c_consumer_test.c`). Its widget is made by the next `rolltui_windows_sync`: a host opening one while
 * handling an event syncs straight after, so the keys that arrived behind it in the same batch reach it. */
int rolltui_window_stack_push_popup(RolltuiWindowStack* s, const RolltuiLayout* layout, const char* id, size_t len);

int rolltui_window_stack_pop(RolltuiWindowStack* s); /* 0 when only the base remains */

/* ---- the layout at run time --------------------------------------------------------------------------
 * WHAT A SCREEN SHOWS CHANGES WITHOUT A NEW LAYOUT. Each call names a window or a split by id, finds it in any layer, and answers 0
 * when nothing has that id. Visibility and size hold until the next `rolltui_window_stack_set_base`; the tab each window shows and
 * the maximised window carry over to a new base wherever their window is still there. */
/* Shown or hidden: a hidden node takes no space, as `"visible": false` in the file. Showing one that would make the layout need more
 * than the screen it was last laid out on is refused: ROLLTUI_STACK_REFUSED, and nothing changes. */
#define ROLLTUI_STACK_REFUSED 2
int rolltui_window_stack_set_visible(RolltuiWindowStack* s, const char* id, size_t len, int visible);
/* WHETHER A LAYOUT FITS a screen `w` by `h`: the least its minimums, fixed sizes, borders, padding and gaps add up to, and its own
 * `min_width` and `min_height`. A host refuses a preset that would not fit with it. 1 or 0. */
int rolltui_layout_fits(const RolltuiLayout* l, int w, int h);
/* HALF THE SCREEN for a window: the rest of the layout in one half and the window in the other, side by side on a wide screen and
 * stacked on a tall one. NULL id puts the layout back. `stack.half` does it for the focused window. */
int rolltui_window_stack_set_half(RolltuiWindowStack* s, const char* id, size_t len);
const char* rolltui_window_stack_half(const RolltuiWindowStack* s, size_t* len);
/* A POPUP AT A PLACE: the topmost popup drawn just under `at` (a row, a window's rect), above it where there is no room below,
 * kept on the screen. Its size is its placement's. */
void rolltui_window_stack_anchor(RolltuiWindowStack* s, RolltuiRect at);
/* A node's size, spelled as the file spells it: cells ("30"), a share ("40%"), "fill" or "fill N". 0 for a spelling that is none. */
int rolltui_window_stack_set_size(RolltuiWindowStack* s, const char* id, size_t len, const char* size, size_t size_len);
/* The window fills its layer alone, and takes the focus, until it is maximised again or a NULL `id` puts the layout back. The
 * `stack.maximize` key does this for the focused window. */
int rolltui_window_stack_maximize(RolltuiWindowStack* s, const char* id, size_t len);
/* The maximised window's id; empty when none is. */
const char* rolltui_window_stack_maximized(const RolltuiWindowStack* s, size_t* len);
/* TABS: a window the file gives `"tabs"` shows one at a time, its titles a strip in its top border. A press on a title, or the
 * `stack.next_tab` and `stack.prev_tab` keys on the focused window, show another, and the route answers
 * ROLLTUI_ROUTE_LAYOUT_CHANGED. Each tab's widget keeps its state while another is shown. */
int rolltui_window_stack_set_tab(RolltuiWindowStack* s, const char* id, size_t len, size_t tab);
/* The tab a window shows, and in `*count` (may be NULL) how many it has: 0 for a window without tabs. */
size_t rolltui_window_stack_tab(const RolltuiWindowStack* s, const char* id, size_t len, size_t* count);

size_t rolltui_window_stack_depth(const RolltuiWindowStack* s);

const RolltuiLayer* rolltui_window_stack_layer(const RolltuiWindowStack* s, size_t i);

int rolltui_window_stack_has_popup(const RolltuiWindowStack* s, const char* id, size_t len);

/* AN `app.<id>` ACTION NAMING A POPUP THE SCREEN DECLARES OPENS IT. Pass the action name (`"app.theme"`); when the part after `app.` names
 * a popup in `layout`, it is toggled and this returns 1; otherwise nothing happens and it returns 0, so a host tests its own actions
 * after calling this and an unknown name falls through. A screen gains a panel by adding one to its layout and one chord to its
 * bindings, with no host code. It TOGGLES: pressing the key that opened a panel closes it. A host that wants something else owns that
 * action itself. As with `_push_popup`, sync the window table after it opens one. */
int rolltui_window_stack_action_popup(RolltuiWindowStack* s, const RolltuiLayout* layout,
                                      const char* action, size_t len);

const RolltuiLayoutNode* rolltui_window_stack_focused(const RolltuiWindowStack* s);
/* NUMBERED WINDOWS: each focusable window of the focus layer has its number before its title, the digit Alt and that digit
 * (`stack.focus_number`) focuses it with. Off by default. */
/* 1: each focusable window's title starts with its number ("2 files"); 2: with the number as a superscript before it ("²files"),
 * plain digits where the terminal draws ambiguous glyphs wide; 0: none. */
void rolltui_window_stack_set_numbered(RolltuiWindowStack* s, int on);
/* A SPINNER IN A WINDOW'S TITLE while it works (`on`), turning from `since_ms`; 0 takes it away. */
void rolltui_window_stack_set_busy(RolltuiWindowStack* s, const char* id, size_t len, int on, unsigned long long since_ms);
/* A WINDOW'S BORDER IN A ROLE OF ITS OWN while it is in a state: searching, an error, a filter applied. 0 gives it back the focus
 * colours. */
void rolltui_window_stack_set_border_role(RolltuiWindowStack* s, const char* id, size_t len, unsigned char role);
/* A SUBTITLE on the top edge, at its right: a live count, a mode. "" takes it away. */
void rolltui_window_stack_set_subtitle(RolltuiWindowStack* s, const char* id, size_t len, const char* text, size_t text_len);
/* A TITLE IN PARTS, each in its own role (0: the title's): a resource, a namespace, a count, a filter. A part with an `action` is a
 * label a press runs: the press routes as ROLLTUI_ROUTE_LABEL, naming the window, and `rolltui_window_stack_pressed_label` gives
 * the action. No parts gives the window its title back. */
typedef struct RolltuiTitlePart {
  const char* text;   /* BORROWED for the call */
  size_t len;
  unsigned char role;
  const char* action; /* BORROWED for the call; NUL-terminated; NULL: not a label */
} RolltuiTitlePart;
void rolltui_window_stack_set_title_parts(RolltuiWindowStack* s, const char* id, size_t len, const RolltuiTitlePart* parts, size_t n);
/* The action of the label the last ROLLTUI_ROUTE_LABEL pressed, into `*out`: 1, or 0 when none was pressed since. */
int rolltui_window_stack_pressed_label(RolltuiWindowStack* s, RolltuiStr* out);

/* WHERE THE POINTER RESTS, for what a host draws outside the windows (a status line's hints): 1 with its cell once the terminal has
 * reported it moving with no button held, which it does only for hover (`RolltuiTerminalOptions::hover`); 0 and (-1, -1) before. */
int rolltui_window_stack_pointer(const RolltuiWindowStack* s, int* x, int* y);

void rolltui_window_stack_focus(RolltuiWindowStack* s, const char* id, size_t len);

/* Every layer resolved against `screen`, in draw order, `focused` set on the one focused
 * window. */
void rolltui_window_stack_resolve(const RolltuiWindowStack* s, RolltuiRect screen, RolltuiResolvedSink emit,
                                  void* ctx);

void rolltui_window_stack_compose(const RolltuiWindowStack* s, RolltuiFrame* f, RolltuiRect screen,
                                  const RolltuiStyle* styles, const RolltuiLayoutRoles* roles,
                                  RolltuiSlotFn render, void* ctx, int ambiguous_wide,
                                  RolltuiComposeScratch* scratch);

/* Routes one event. `window` is filled with the target id — a COPY, because the
 * ClosedPopup case names a layer this call has already freed. A mouse event also tells the stack where the pointer is, which
 * `_compose` and `_resolve` mark on the window it rests on (`RolltuiResolvedNode::hover`). */
unsigned char rolltui_window_stack_route(RolltuiWindowStack* s, const RolltuiEvent* e, RolltuiRect screen,
                                         const RolltuiBindings* bindings, const RolltuiStackActions* actions,
                                         RolltuiStr* window);

RolltuiInput* rolltui_windows_input(RolltuiWindows* w, const char* source, size_t len);

RolltuiTranscript* rolltui_windows_transcript(RolltuiWindows* w, const char* source, size_t len);

RolltuiMenu* rolltui_windows_menu(RolltuiWindows* w, const char* source, size_t len);

/* Which rung answered for `menus/<source>.json`: the file's path, "the host's", "a shipped
 * menu", or "" when nothing did. Re-resolves first; a BORROW until the next refresh. */
const char* rolltui_windows_menu_origin(RolltuiWindows* w, const char* source, size_t len, size_t* out_len);

/* …and the same three by WINDOW id: the typed handle that window's widget draws, or NULL when the window is unknown or its content is a
 * different kind. A window that has never been synced answers NULL, which is not an error. */
RolltuiMenu* rolltui_windows_menu_at(const RolltuiWindows* w, const char* window, size_t len);

/* That window's widget closes one inner level (its `back` slot); 0 when it has none. The shape of `RolltuiStackLevelFn`, so
 * `rolltui_window_stack_set_level_fn(stack, rolltui_windows_back, windows)` is the whole wiring. */
int rolltui_windows_back(void* windows, const char* window, size_t len);

/* WHERE A WINDOW LANDED this frame: its outer rectangle, border included, as the last `rolltui_windows_layout` placed it; 0 when no
 * window has that id. A host that draws something of its own over a window (a breadcrumb over its path line) asks here. */
int rolltui_windows_window_rect(const RolltuiWindows* w, const char* window, size_t len, RolltuiRect* out);
/* That window's content string, a BORROW valid until the next `sync`. */
const char* rolltui_windows_content_at(const RolltuiWindows* w, const char* window, size_t len,
                                       size_t* out_len);

/* Instantiates/reuses a widget per window and collects what each one says is wrong. The
 * report's lines are BORROWS, valid until the next sync. */
void rolltui_windows_sync(RolltuiWindows* w, const RolltuiWindowStack* stack);

size_t rolltui_windows_report_count(const RolltuiWindows* w);

const char* rolltui_windows_report_at(const RolltuiWindows* w, size_t i, size_t* len);

/* The one-line form: the first bad value, plus " (+N more)" when there are others; "" when there are none. The rule lives with the report
 * because every host draws this string. APPENDS to `out`. */
void rolltui_windows_report_summary(const RolltuiWindows* w, RolltuiStr* out);

/* Asks each widget for the outer extent it wants and writes it into the node (the only thing
 * a widget writes back into the layout tree). */
void rolltui_windows_autosize(RolltuiWindows* w, RolltuiWindowStack* stack, RolltuiRect box);

void rolltui_windows_layout(RolltuiWindows* w, const RolltuiWindowStack* stack, RolltuiRect box);

/* The library's own three, for the same reason as `rolltui_layout_default_roles`. BORROWS static storage; a host that paints its
 * scrollbar from another role still passes its own struct. */
const RolltuiWindowRoles* rolltui_windows_default_roles(void);

/* THE FOUR CELLS A SCROLLBAR THUMB IS MADE OF, so a look is a theme's rather than a literal. A thumb is a CAPSULE: `single` when it is
 * one cell tall, otherwise `top`, then `middle` repeated, then `bottom` (half-blocks, each filling only the half of its cell facing
 * inward, so a bar of any length has soft ends and a solid body). Each glyph is one grapheme in eight bytes, copied on set, so a theme's
 * parsed text need not outlive the call. `ascii_*` is used when the terminal draws East Asian AMBIGUOUS glyphs two cells wide (every
 * glyph worth using here is ambiguous, as the box-drawing borders are). */
typedef struct RolltuiScrollbarGlyphs {
  char single[8], top[8], middle[8], bottom[8];
  char ascii_single[4], ascii_top[4], ascii_middle[4], ascii_bottom[4];
  /* THE CURSOR ROW'S SHAPE in lists, menus and the column browser (`glyphs.cursor`: "fill", "caps", "edges"): ROLLTUI_CURSOR_* */
  unsigned char cursor;
  /* THE SYMBOLS a chart with none of its own draws in (`glyphs.chart`: "braille", "blocks", "shades"): ROLLTUI_CHART_SYMBOLS_* */
  unsigned char chart;
} RolltuiScrollbarGlyphs;

/* What a host does with the pair: READ a theme's answer, APPLY it. The default set and the read-back live in the library's own header,
 * since a theme a host did not write still fills every slot. */
void rolltui_context_set_scrollbar_glyphs(RolltuiContext* ctx, const RolltuiScrollbarGlyphs* g);

/* Reads a theme's `glyphs.scrollbar` object into `out`, filling every key it does not state with the shipped default, so `out` is always
 * complete. Returns 0 when the theme says nothing. Separate from `rolltui_theme_load` because it answers a different question. */
int rolltui_theme_scrollbar_glyphs(const RolltuiJsonValue* root, RolltuiScrollbarGlyphs* out);

void rolltui_windows_draw(RolltuiWindows* w, const RolltuiResolvedNode* rn, RolltuiFrame* f,
                          const RolltuiStyle* styles, const RolltuiWindowRoles* roles);

/* An event the stack routed to `window`; 1 when the widget (or its scrollbar) consumed it. */
int rolltui_windows_handle(RolltuiWindows* w, const char* window, size_t len, const RolltuiEvent* e);
/* THE KEYS ON THE WAY: a count typed and the first keys of a sequence (`5`, `g`), as they are shown, into `*text` (may be NULL); how
 * many there are, 0 when nothing is on the way. A status line shows it as vim does. `rolltui_windows_draw_toasts` draws, after a
 * moment, a panel of what can follow. */
size_t rolltui_windows_pending_keys(const RolltuiWindows* w, RolltuiStr* text);
/* A HOST'S OWN SCOPE (`app`) with the sequences a window's has: call it for each key before routing the key to a window. DONE: the key
 * finished a chord or a sequence bound in `scope`, and `*action` (may be NULL) names it for the host to do. PREFIX: the key is on the
 * way to one (or Escape let go of one), and nothing else takes it. NONE: `scope` takes no part in it; route it on. THE WINDOW WITH
 * FOCUS COMES FIRST: a key it takes (its plugin's `takes`) is NONE here, so a program's own keys never stop the focused window working.
 * A sequence already on the way in a window's scope is left to that window. */
int rolltui_windows_app_key(RolltuiWindows* w, const RolltuiEvent* e, const char* scope, size_t scope_len, const char** action,
                            size_t* action_len);
/* Whether the window with focus takes `key` itself (its plugin's `takes`, else a key bound in its own scope). A host that matches keys
 * of its own outside `rolltui_windows_app_key` asks this first, and leaves a key the focused window takes to that window. */
int rolltui_windows_focused_takes(const RolltuiWindows* w, const RolltuiChord* key);
/* THE KEY AN `any` TOOK: after a sequence ending in `any` is DONE (`"m any"`), or after a key asked for, the character typed there.
 * 1 and `*out` filled while there is one; it lasts until the next sequence or ask. */
int rolltui_windows_any_key(const RolltuiWindows* w, RolltuiChord* out);
/* ASK FOR ONE KEY: the next key is not routed anywhere; `rolltui_windows_app_key` answers it DONE with `action`, and
 * `rolltui_windows_any_key` gives the key (a mark's letter, y or n). Escape lets go of the question. `prompt` stands in the keys
 * panel meanwhile, at once. Both are copied. */
void rolltui_windows_ask_key(RolltuiWindows* w, const char* action, size_t action_len, const char* prompt, size_t prompt_len);

/* ========================================================================================
 * filepicker — the column browser, and directory reading a host can use on its own
 * ======================================================================================== */

#define ROLLTUI_SORT_NAME 0 /* natural order (`rolltui_natural_compare`): file2 before file10, case and accents aside */
#define ROLLTUI_SORT_SIZE 1
#define ROLLTUI_SORT_MODIFIED 2

typedef struct RolltuiDirEntry {
  RolltuiStr name;      /* OWNED by the list */
  int is_dir;
  long long size;       /* bytes; 0 for a directory */
  long long modified;   /* seconds, for sorting and for a host to format */
  unsigned int mode;    /* the permission bits, for a browser that shows them; 0 if unreadable */
  int unreadable;       /* the entry is there and could not be described — a broken link, say */
} RolltuiDirEntry;

/* GROWING AMORTISED. Zero-initialise before first use; `_release` frees every name and the array and zeroes it (a no-op on a zeroed list
 * and on NULL). REUSED across reads, so walking a tree does not allocate per directory. */
typedef struct RolltuiDirList {
  RolltuiDirEntry* v;
  size_t n, cap;
  /* HOW MANY WERE HIDDEN, so a browser can say so. "3 hidden" and "nothing here" are different
   * answers and a count is the only way to tell them apart after the fact. */
  size_t hidden_n;
} RolltuiDirList;

void rolltui_dir_list_release(RolltuiDirList* l);

/* FILES AND FOLDERS WATCHED. What decides a change is each path's signature (its device, inode, size and times) against the last
 * look, so a save that writes a new file and renames it over the old one, a path that does not exist yet and arrives, a file
 * removed, and a folder's entries arriving or leaving are all told. The system says when to look (kqueue on macOS and the BSDs,
 * inotify on Linux, change notifications on Windows); where it cannot, a thread looks twice a second, and with no thread at all
 * (a browser) `rolltui_watch_changed` looks. `wake` (may be NULL) is called from the watcher's thread when something changed, so a
 * loop asleep in its input wait looks at once (`rolltui_terminal_wake`). OWNED. */
typedef struct RolltuiWatch RolltuiWatch;
RolltuiWatch* rolltui_watch_new(void (*wake)(void* ctx), void* wake_ctx);
void rolltui_watch_free(RolltuiWatch* w); /* a no-op on NULL */
/* An id for `path`, never 0; 0 for a path that is empty or holds a NUL. */
unsigned int rolltui_watch_add(RolltuiWatch* w, const char* path, size_t len);
void rolltui_watch_remove(RolltuiWatch* w, unsigned int id);
/* The ids that changed since the last call, into `out` (`out` NULL: how many, and they are told no more); how many were put there.
 * Each change is told once; past `cap`, the rest wait for the next call. */
size_t rolltui_watch_changed(RolltuiWatch* w, unsigned int* out, size_t cap);
/* How it is told: "kqueue", "inotify", "change notifications", or "a look on the clock". */
const char* rolltui_watch_how(const RolltuiWatch* w);

/* Fills `out` with `path`'s entries, sorted by `sort`, `.`-prefixed names included only when `hidden` is non-zero. Directories sort
 * before files at every sort. Returns 1 on success. On failure `out` is left EMPTY and `err` (may be NULL) gets the reason: a directory
 * that cannot be read and one that is empty are different answers. */
#define ROLLTUI_DIR_HIDDEN 1  /* include `.`-prefixed names */
#define ROLLTUI_DIR_LINKS 2   /* describe a symlink itself rather than what it points at */
#define ROLLTUI_DIR_REVERSED 4 /* the sort's order turned around — z to a, smallest first, oldest first — folders still before files */

/* READING A DIRECTORY, PUBLIC because a host writing a rich browser (Miller columns, metadata, its own sort) needs what `filepicker` does
 * underneath: the entries, sorted, dotfiles shown or hidden, and a NAMED reason when the directory cannot be read (an unreadable
 * directory and an empty one both yield no entries). `flags` is a bitmask of the two above: separate flags rather than a bool because a
 * browser and a picker disagree about symlinks (a picker wants the TARGET, so choosing a link to a directory enters it; a browser shows
 * what is on disk and wants the LINK). */
int rolltui_dir_read(const char* path, size_t len, int sort, int flags, RolltuiDirList* out,
                     RolltuiStr* err);

/* ---- `filepicker`: the column browser, and the calls a host makes ---------------------------
 * Miller columns: every column is one directory with its own cursor and scroll, the column right of the focus previews what the
 * cursor is on, and the columns run from `/` down to where the picker was pointed so a deep start shows its ancestors. A divider
 * after each column carries that column's thumb; a column clipped at the left edge fades. What a chosen path MEANS is the host's: the
 * picker records an event and a host reads it; it opens nothing and ends nothing.
 * Where it starts can be set at any time; a picker with nowhere to start looks at the working directory. */
void rolltui_windows_set_picker_dir(RolltuiWindows* w, const char* content, size_t len, const char* dir,
                                    size_t dir_len);

/* The answer, POLLED: 1 exactly once per choice, filling `out` with the chosen path; 0 otherwise. Ask on the frame after opening the
 * panel, where you already ask a preset store for its version. The narrow form of `rolltui_windows_picker_event`: a cancel or a copy is
 * dropped here. */
int rolltui_windows_picker_taken(RolltuiWindows* w, const char* content, size_t len, RolltuiStr* out);

/* WHAT THE PICKER'S KEYS SAID, which only a host can act on: a path TAKEN (Enter on a file, or on a folder when `take_folders` is set;
 * else Enter enters it), CANCELLED (the `picker.cancel` chord), or a COPY of the selection's path asked for (`picker.copy`; `inverse` set
 * by the `copy_inverse` chord, "the other way round from my setting"). Collected once, like `_taken`. `path` is OWNED by the event:
 * release it. */
#define ROLLTUI_PICKER_EVENT_NONE 0
#define ROLLTUI_PICKER_EVENT_TAKEN 1
#define ROLLTUI_PICKER_EVENT_CANCELLED 2
#define ROLLTUI_PICKER_EVENT_COPY 3
/* With `report_changes`: the cursor is on another entry (`path` the entry's), and the folder shown in the focused column changed (`path`
 * the folder's). Each is told once a change, the newest last, after the outcomes above. */
#define ROLLTUI_PICKER_EVENT_MOVED 4
#define ROLLTUI_PICKER_EVENT_FOLDER 5
typedef struct RolltuiPickerEvent {
  unsigned char kind ROLLTUI_DEFAULT(0);
  unsigned char inverse ROLLTUI_DEFAULT(0);
  RolltuiStr path;
} RolltuiPickerEvent;
void rolltui_picker_event_release(RolltuiPickerEvent* e);
int rolltui_windows_picker_event(RolltuiWindows* w, const char* content, size_t len, RolltuiPickerEvent* out);
/* A HOST'S PREVIEWER, asked first for each file the column browser previews: it fills `out` (another program's output, a document it
 * renders) and answers 1, or answers 0 and the built-in preview draws the file. `offset` is where the person has scrolled to: a line, or
 * with `*bound` set, a place of the previewer's own (a page, a percent) from 0 to `*bound` - 1, and each scroll there asks again at the
 * new offset. `out` stays the preview's until the next ask, so a previewer may feed it as a command's output arrives. */
typedef int (*RolltuiPreviewerFn)(void* ctx, const char* path, size_t len, size_t offset, RolltuiAnsiText* out, size_t* bound);
void rolltui_windows_set_picker_previewer(RolltuiWindows* w, const char* content, size_t len, RolltuiPreviewerFn fn, void* ctx);
/* A COMMAND AS THE PREVIEWER: `argv` (NULL-terminated, copied; NULL turns it off) run for each file previewed, with the file's path,
 * the preview's width and its height after its own arguments, on a pseudo-terminal that size, so it writes colour. What it writes is
 * the preview, drawn as it arrives. The cursor resting 30 ms on a file starts it, so a cursor passing over files starts nothing; moving
 * away stops it. A command that cannot start leaves the built-in preview; a host's previewer is asked before it. */
void rolltui_windows_set_picker_preview_command(RolltuiWindows* w, const char* content, size_t len, const char* const* argv);
/* The paths marked (`marks`), into `out` (may be NULL); how many. */
size_t rolltui_windows_picker_marked(RolltuiWindows* w, const char* content, size_t len, RolltuiStrListRaw* out);
/* AN ENTRY'S OWN MARKS, from the host, asked for each entry drawn: a glyph in the cell before its name (copied, cut, tagged) in
 * `gutter_role`; a short cell of text right of the name (a git status, a count) in `cell_role`; the name in `role` (0: as it is). */
typedef struct RolltuiPickerDecor {
  char gutter[8];
  unsigned char gutter_role ROLLTUI_DEFAULT(0);
  char cell[32];
  unsigned char cell_role ROLLTUI_DEFAULT(0);
  unsigned char role ROLLTUI_DEFAULT(0);
} RolltuiPickerDecor;
typedef void (*RolltuiPickerDecorFn)(void* ctx, const char* path, size_t len, int is_dir, RolltuiPickerDecor* out);
void rolltui_windows_set_picker_decor(RolltuiWindows* w, const char* content, size_t len, RolltuiPickerDecorFn fn, void* ctx);

/* THE PICKER'S SETTINGS, a host's to set: dotfiles shown, the sort (`ROLLTUI_SORT_*`), motion (the column slide only; off snaps),
 * highlight (a background block on the cursor's row and the trail's, independent of motion; a host that also turns its own glow effect
 * off should leave one of the two on or the cursor stops being visible), the dividers, and whether Enter on a folder TAKES it (a
 * directory picker) or ENTERS it (a file dialog, the default). `_init` fills the defaults. */
typedef struct RolltuiPickerOptions {
  unsigned char hidden ROLLTUI_DEFAULT(1);
  unsigned char sort ROLLTUI_DEFAULT(0);
  unsigned char motion ROLLTUI_DEFAULT(1);
  unsigned char highlight ROLLTUI_DEFAULT(1);
  unsigned char dividers ROLLTUI_DEFAULT(1);
  unsigned char take_folders ROLLTUI_DEFAULT(0);
  /* The sort's order turned around: name z to a, smallest first, oldest first. */
  unsigned char reversed ROLLTUI_DEFAULT(0);
  /* A SIZE and a MODIFIED column after the name, right-aligned, each one of `ROLLTUI_SHOW_*`: WITH_SORT (the default) shows the column
   * while its key is the sort; ALWAYS and NEVER are a person's own word. */
  unsigned char show_size ROLLTUI_DEFAULT(0);
  unsigned char show_modified ROLLTUI_DEFAULT(0);
  /* WHAT A FILE LOOKS LIKE, beside the cursor: `ROLLTUI_PREVIEW_OFF` (the default) shows nothing right of a file; `ROLLTUI_PREVIEW_RIGHT`
   * reserves the right half and shows the file there (its lines as text, Markdown rendered with a mermaid diagram drawn as one, a binary
   * file as offsets, byte pairs and an ASCII gutter). A folder under the cursor behaves as ever, in the first cells of that same half, so
   * nothing moves as the cursor passes from a file to a folder. Right on a file moves the keys into the preview (Up, Down, PgUp, PgDn,
   * Home, End scroll it); Right AGAIN gives it the whole picker; Left or Escape step back one place at a time; the wheel scrolls it
   * wherever the keys are. A text file is read whole up to 256 KB and its first 256 KB shown beyond that. */
  unsigned char preview ROLLTUI_DEFAULT(0);
  /* WATCHING, ON UNLESS SAID OTHERWISE (an opt out, so a zeroed struct means "watch"): every folder with its insides on screen and the
   * file being previewed is looked at again about twice a second by `stat` (no descriptor held), and what moved is read again. A grown,
   * added, removed or renamed file and a folder that goes or comes back are on screen within the interval, with the cursor and scroll
   * where they were (by name; a selection that is gone falls to the entry now in its place) and a preview kept at the same line or at the
   * end it was following. A slow disk backs the interval off. Nothing runs without a frame clock, so a golden frame is a still. */
  unsigned char no_watch ROLLTUI_DEFAULT(0);
  /* SOURCE COLOUR, ON UNLESS SAID OTHERWISE (an opt out): a previewed file in a language the library knows (by its name, extension or first
   * line) is drawn with its keywords, strings, comments and numbers in the theme's colours and the head names the language; a Markdown
   * document's fenced code is coloured the same way. The languages are JSON files: the shipped ones, and any a person keeps in
   * `<config>/rolltui/syntax/` (one named like a shipped language replaces it). A file in no known language is drawn plain. */
  unsigned char no_syntax ROLLTUI_DEFAULT(0);
  /* WHAT THE CURSOR IS ON, SAID WITHOUT ASKING (off unless a host sets it): a second row under the column heads holding, at the head of
   * whatever is right of the cursor, the entry's mode and when it was last written (a folder's over the column that lists it, a file's
   * under the head of its preview). With no preview the slot right of a file is a small pane of its own: the file's name, its size unless
   * a size column already says it, and the same second row. The columns lose one row for it, and a picker too short to spare one shows
   * none. A slot is kept right of the focus whatever is under the cursor, so nothing moves as it passes from a file to a folder. */
  unsigned char info ROLLTUI_DEFAULT(0);
  /* SCROLLING: rows kept between the cursor and a column's top or foot (past half the view, centred), and the cursor going from the
   * last entry to the first and back. */
  int scrolloff ROLLTUI_DEFAULT(0);
  unsigned char wrap ROLLTUI_DEFAULT(0);
  /* CHANGES REPORTED: ROLLTUI_PICKER_EVENT_MOVED and _FOLDER events as the cursor and the folder change (0: only the outcomes). */
  unsigned char report_changes ROLLTUI_DEFAULT(0);
  /* THE PANES' WIDTHS AS SHARES, a folder above, the one the cursor is in and what is beside it: 1, 4, 3 gives them an eighth, a half and
   * three eighths of the width. A share of 0 leaves that pane its own width, its names' (the slot beside the cursor, 28 cells or half
   * the picker with a preview). */
  unsigned char ratio_parent ROLLTUI_DEFAULT(0), ratio_current ROLLTUI_DEFAULT(0), ratio_next ROLLTUI_DEFAULT(0);
  /* THE FILE PREVIEW'S LINES: the columns between tab stops (0: four), and a line wider than the preview wrapping onto the rows below
   * (`preview_wrap`) instead of ending in an ellipsis. */
  unsigned char preview_tab ROLLTUI_DEFAULT(0), preview_wrap ROLLTUI_DEFAULT(0);
  /* MARKS: entries chosen several at once, in any folder, by path: Space marks and moves on, Shift-Up and Shift-Down mark as they
   * go, Alt-U clears them. A marked entry is bold in the mark's colour with ▌ before it. */
  unsigned char marks ROLLTUI_DEFAULT(0);
} RolltuiPickerOptions;
#define ROLLTUI_PREVIEW_OFF 0
#define ROLLTUI_PREVIEW_RIGHT 1
#define ROLLTUI_SHOW_WITH_SORT 0
#define ROLLTUI_SHOW_ALWAYS 1
#define ROLLTUI_SHOW_NEVER 2
void rolltui_picker_options_init(RolltuiPickerOptions* o);
void rolltui_windows_set_picker_options(RolltuiWindows* w, const char* content, size_t len,
                                        const RolltuiPickerOptions* o);

/* What is under the cursor (the focused column's directory when that column is empty) and whether it is a folder (a link to one
 * counts); and the focused column's directory itself, which a save dialog joins a typed name onto. Both fill a caller's string and
 * return 1 when there is a picker to ask. */
int rolltui_windows_picker_selected(RolltuiWindows* w, const char* content, size_t len, RolltuiStr* path,
                                    int* is_dir);
int rolltui_windows_picker_dir(RolltuiWindows* w, const char* content, size_t len, RolltuiStr* out);
/* A BUNDLE (a directory named `.app`) is a LEAF: listed without the folder's chevron, never entered, taken by Enter like a file. What a
 * host does with it is the host's (dirktui opens it).
 * FOCUS A COLUMN THAT IS OPEN: the i-th from the left, the root's being 0, keeping its preview and closing the columns deeper than that,
 * as Left does (what a breadcrumb's segment means). Past the last column lands on the last. */
void rolltui_windows_picker_focus_column(RolltuiWindows* w, const char* content, size_t len, size_t column);

/* THE FACTS A STATUS LINE WANTS, in one query: how many entries the focused column shows and hid, which column of how many the cursor is
 * in, whether the columns are mid-slide (a host's frame timer asks for the next frame soon while they are), and why the first column
 * could not be read, when it could not. `error` is OWNED by the status: release it. */
typedef struct RolltuiPickerStatus {
  size_t entries ROLLTUI_DEFAULT(0);
  size_t hidden ROLLTUI_DEFAULT(0);
  size_t column ROLLTUI_DEFAULT(0);  /* 1-based */
  size_t columns ROLLTUI_DEFAULT(0);
  unsigned char moving ROLLTUI_DEFAULT(0);
  /* THE FILE PREVIEW (`RolltuiPickerOptions.preview`): 0 nothing is shown, 1 a file is shown beside the cursor, 2 the keys are in it (Up,
   * Down, PgUp, PgDn, Home, End scroll it; Right gives it the whole picker; Left or Escape step back), 3 it has the whole picker. */
  unsigned char preview ROLLTUI_DEFAULT(0);
  /* WHEN THE PICKER NEXT WANTS TO LOOK AT THE DISK, in milliseconds from the last frame; 0 when it never does (watching is off, or there
   * is no frame clock). A host's wait for input is at most this. */
  int wake_ms ROLLTUI_DEFAULT(0);
  RolltuiStr error;
} RolltuiPickerStatus;
void rolltui_picker_status_release(RolltuiPickerStatus* s);
int rolltui_windows_picker_status(RolltuiWindows* w, const char* content, size_t len, RolltuiPickerStatus* out);

/* THE TWELVE ACTION NAMES the picker's keys are resolved against, in the `picker` scope. */
typedef struct RolltuiPickerActions {
  const char* up;
  const char* down;
  const char* page_up;
  const char* page_down;
  const char* first;
  const char* last;
  const char* into;
  const char* out;
  const char* take;
  const char* cancel;
  const char* copy;
  const char* copy_inverse;
  const char* half_up;
  const char* half_down;
  /* THE FOLDERS VISITED: back to the one before, forward again, and to the one last left (as `cd -`) */
  const char* back;
  const char* forward;
  const char* previous;
  const char* mark;
  const char* mark_up;
  const char* mark_down;
  const char* unmark_all;
  const char* preview_left;
  const char* preview_right;
  /* the preview scrolled a wheel's notch while the focus stays in the columns */
  const char* preview_up;
  const char* preview_down;
} RolltuiPickerActions;

/* ---- a hint bar: the keys a status line names, clickable ---------------------------------
 * "F1 help  F2 settings  c copy": each hint is a chord's text, a label, and the ACTION they stand for. Drawn once per frame, and the
 * bar remembers where every hint landed, so a press at a cell answers with the action and a host runs it as it would the key. A hint
 * that does not fit whole is left out, never cut, and is not hittable. The strings are copied; `_clear` empties the bar for a rebuild
 * when the bindings change. OWNED by the host: `_free` releases it. */
typedef struct RolltuiHintBar RolltuiHintBar;
RolltuiHintBar* rolltui_hint_bar_new(void);
void rolltui_hint_bar_free(RolltuiHintBar* b);
void rolltui_hint_bar_clear(RolltuiHintBar* b);
void rolltui_hint_bar_add(RolltuiHintBar* b, const char* chord, size_t chord_len, const char* label, size_t label_len,
                          const char* action, size_t action_len);
/* EVERY ACTION OF A SCOPE as a hint, in the table's order: its first key this terminal can send, and what it does. How many were
 * added. A program's own bar for its `app` scope is this one call. */
size_t rolltui_hint_bar_add_scope(RolltuiHintBar* b, const RolltuiBindings* bindings, const char* scope, size_t len);
/* What goes BETWEEN hints (two spaces by default; " › " makes a breadcrumb), and whether the bar keeps its TAIL when short of room (the
 * last hints drawn whole, the head replaced by an ellipsis) rather than dropping whichever hints do not fit. A breadcrumb keeps its tail:
 * the place the eye is at, and the pencil after it, must always be there to click. With the tail kept the LAST hint is the bar's own mark
 * rather than a part, and a single space joins it. */
void rolltui_hint_bar_set_separator(RolltuiHintBar* b, const char* sep, size_t len);
void rolltui_hint_bar_set_keep_tail(RolltuiHintBar* b, int on);
/* A hint whose action cannot be taken now is DISABLED: drawn in `muted`, never hit. Set by
 * action name, every frame if need be — it allocates nothing. */
void rolltui_hint_bar_enable(RolltuiHintBar* b, const char* action, size_t len, int on);
/* WHICH GO FIRST when the bar is short of room: hints that rank lower are left out before any that rank higher, and the rest keep
 * their order (0, every hint's to begin with: whichever do not fit go). */
void rolltui_hint_bar_set_priority(RolltuiHintBar* b, const char* action, size_t len, int priority);
/* THE POINTER, for the next draw: the hint whose cells hold (x, y) is drawn in `hover`, chord and label alike, as a button lit under
 * it; (-1, -1) for none. A disabled hint is never lit. Set every frame from `rolltui_window_stack_pointer`; it allocates nothing. */
void rolltui_hint_bar_hover(RolltuiHintBar* b, int x, int y, RolltuiStyle hover);
/* Draws from (x, y) within `width` cells and returns the cells used. */
int rolltui_hint_bar_draw(RolltuiHintBar* b, RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, int width,
                          RolltuiStyle chord_style, RolltuiStyle label_style, RolltuiStyle muted, int ambiguous_wide);
/* The action drawn under (x, y) on the last draw — a BORROW into the bar — or NULL. */
const char* rolltui_hint_bar_hit(const RolltuiHintBar* b, int x, int y, size_t* len);
/* ========================================================================================
 * BOOK 4 — RUNTIME: the terminal, the double buffer, the run loop, the session
 * What ties a screen together frame to frame: the one fd, presenting a frame at the right depth, the session's registries and
 * caches, shutdown.
 * ======================================================================================== */

/* ========================================================================================
 * THE SESSION — make one of these FIRST; everything a host does hangs off it
 * ======================================================================================== */

/* A ROLLTUI SESSION: the registries and caches an app configures, and nothing else (as process-wide statics they would make two
 * apps in one process share one widget-kind registry).
 *
 * THE CONTRACT
 *   1. One thread at a time: the library locks nothing inside a context. The exception is the theme/layout/bindings stores, each of
 *      which carries its own mutex so a host may edit from one thread and render from another — rendering from a store's
 *      `_copy_working`, never from `_working`'s borrow, which a load on the other thread frees.
 *   2. ANY NUMBER OF CONTEXTS, same thread or different, configured alike or differently: a plain owned handle with no thread
 *      affinity, sharing nothing with another.
 *   3. LAYOUTS, THEMES AND BINDINGS TABLES ARE PLAIN DATA, PORTABLE BETWEEN CONTEXTS. Kind resolution happens at
 *      `rolltui_windows_sync`, not at load, so an unknown kind is a runtime error PANEL and not a load failure, and one layout may
 *      drive two contexts, each resolving kinds against its own registry.
 *   4. A CACHED BUILT-IN BELONGS TO THE CONTEXT THAT CACHED IT: reading one from another context is fine; outliving its owner is not.
 *   5. ONLY ONE CONTEXT MAY DRIVE A TERMINAL (one controlling terminal, one saved `termios`, one signal disposition). Any number may
 *      build screens and render to TEXT headless, as every test and `--frame` run does.
 *   6. The allocator counters are a process-wide atomic SUM: assert `live_bytes == 0` after freeing ALL contexts, not per context.
 *
 * OWNED: `_new` / `_free`, and `_free` is a no-op on NULL. */
RolltuiContext* rolltui_context_new(void);
void rolltui_context_free(RolltuiContext* c);

/* ========================================================================================
 * terminal — the one fd
 * ======================================================================================== */

/* ---- options, defined once and compiled by both languages ------------------------------ */
/* The attribute bits are `unsigned char`, not `bool`, for `rolltui_style.h`'s reason: one type in both languages, nothing to assume
 * about `_Bool` versus `bool`. */
#define ROLLTUI_DOUBLE_CLICK_MS 400 /* two presses of one button on one cell this close together are a double-click */

typedef struct RolltuiTerminalOptions {
  unsigned char alt_screen ROLLTUI_DEFAULT(1);
  /* INLINE: the program is drawn in the normal screen, from the row the cursor was on, instead of on the alternate screen, the way
   * `fzf --height` and a chat command line are. The region is as tall as the frame presented (`rolltui_run` presents `inline_rows`
   * rows; a host with its own loop draws the height it wants), room is made below by scrolling, rows a host commits stay above it as
   * ordinary output (`rolltui_terminal_commit`), and leaving erases it and leaves the cursor where it began. 0: the whole screen.
   * `alt_screen` is ignored when this is set. */
  unsigned short inline_rows ROLLTUI_DEFAULT(0);
  unsigned char mouse ROLLTUI_DEFAULT(1);        /* SGR 1006 + button + drag reporting */
  /* HOVER: the terminal also reports the pointer as it moves with no button held (mode 1003), and what is under it is drawn
   * hovered: the row a click would act on, in the theme's `hover` role (see `RolltuiResolvedNode::hover`). The keyboard's cursor
   * never moves with it. Needs `mouse`. Off by default: a host that draws no hover gains only events. */
  unsigned char hover ROLLTUI_DEFAULT(0);
  unsigned char bracketed_paste ROLLTUI_DEFAULT(1);
  unsigned char hide_cursor ROLLTUI_DEFAULT(1);
  unsigned char handle_signals ROLLTUI_DEFAULT(1); /* restore-and-reraise on INT/TERM/HUP/QUIT */
  /* WHAT THE TERMINAL IS (colour depth, light or dark, how wide an ambiguous glyph draws) is found out at entry and remembered (see
   * `RolltuiTermFacts`), so every option below is an opt OUT: a host that says nothing gets the right answer, and a zero-initialised C
   * struct means the same as a default-initialised C++ one. */
  unsigned char no_probe ROLLTUI_DEFAULT(0); /* ask the terminal nothing but the keyboard question */
  unsigned char no_cache ROLLTUI_DEFAULT(0); /* neither read nor write the remembered answers */
  /* Added to what the remembered answers are filed under. A program's own name, size and modification time are ALWAYS part of it, so a new
   * build asks the terminal again; this is for a host that wants more (a configuration name). A BORROW for the `rolltui_terminal_new`
   * call only. */
  const char* app_key ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* Send me `ROLLTUI_TERM_EVENT_FACTS` when a remembered answer turns out stale and changes what I draw. Off by default because a host that
   * does not know the event is better off never being sent one; the facts are corrected either way, and the library's own consumers of
   * them (present depth, glyph width, theme mode) see the correction regardless. */
  unsigned char facts_events ROLLTUI_DEFAULT(0);
  /* Where the remembered answers live. A BORROW for the call only. NULL: the person's rolltui configuration directory (ROLL_CONFIG_DIR,
   * else XDG_CONFIG_HOME/roll/rolltui, else ~/.config/roll/rolltui). */
  const char* cache_dir ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* HOW PICTURES ARE DRAWN, said outright (ROLLTUI_IMAGES_*): a host's setting the person chose. 0: found out (`RolltuiTermFacts`). */
  unsigned char images ROLLTUI_DEFAULT(0);
  /* A PICTURE HELD before its bytes are sent, until it has stayed in the same cells this long: a cursor moving over files sends none
   * of the pictures it passes. 0: 60 ms; -1: sent at once. `rolltui_terminal_pending_ms` says when one is due. */
  int picture_delay_ms ROLLTUI_DEFAULT(0);
} RolltuiTerminalOptions;

/* ---- events ----------------------------------------------------------------------------- */
/* The same three kinds `rolltui_keys.h` defines, plus RESIZE — see rule 4 above. */
#define ROLLTUI_TERM_EVENT_KEY ROLLTUI_EVENT_KEY

#define ROLLTUI_TERM_EVENT_MOUSE ROLLTUI_EVENT_MOUSE

#define ROLLTUI_TERM_EVENT_PASTE ROLLTUI_EVENT_PASTE

#define ROLLTUI_TERM_EVENT_RESIZE 3

/* THE TERMINAL LEARNED SOMETHING that changes what a host should draw (today: its background flipped between light and dark since the
 * answers were remembered). Read `rolltui_terminal_facts` again and re-resolve the theme. Sent only to a host that asked
 * (`RolltuiTerminalOptions::facts_events`), and only after a remembered answer was used and then checked against the terminal. A host
 * ignores kinds it does not know. */
#define ROLLTUI_TERM_EVENT_FACTS 4
/* The window gained or lost the person's focus; `focused` says which. A light or dark change the terminal reports while the program
 * runs (mode 2031) is a FACTS event: the background's side is a fact. */
#define ROLLTUI_TERM_EVENT_FOCUS ROLLTUI_EVENT_FOCUS

/* ---- what the terminal is ------------------------------------------------------------------
 * Facts no host should have to remember to ask: how many colours the terminal can show, whether its background is light or dark, how
 * wide it draws an East Asian AMBIGUOUS glyph. Found out at entry, remembered per terminal, and re-checked in the background when
 * they were remembered rather than asked, so a wrong answer costs one frame, not a session. Every fact says WHERE IT CAME FROM
 * ("the environment said so" and "the terminal said so" deserve different trust). */
#define ROLLTUI_FACT_DEFAULT 0 /* nothing said anything; the conservative answer */
#define ROLLTUI_FACT_ENV 1     /* read from the environment */
#define ROLLTUI_FACT_PROBE 2   /* the terminal answered a question */
#define ROLLTUI_FACT_CACHE 3   /* remembered from an earlier run on this same terminal */
#define ROLLTUI_FACT_FORCED 4  /* said outright: ROLL_COLOR_DEPTH, a theme's depth, a host's call */

typedef struct RolltuiTermFacts {
  unsigned char depth;          /* ROLLTUI_DEPTH_*: what to draw at */
  unsigned char depth_source;   /* ROLLTUI_FACT_* */
  unsigned char mode;           /* ROLLTUI_MODE_DARK or ROLLTUI_MODE_LIGHT: what the background is */
  unsigned char mode_source;
  unsigned char has_background; /* `background` is a colour the terminal reported */
  RolltuiStyleColorRaw background;
  unsigned char ambiguous_wide; /* 1: an ambiguous-width glyph takes two cells */
  unsigned char ambiguous_source;
  /* 1: the terminal gives every code point cells of its own, a zero-width one (a combining accent, a joiner, a variation
   * selector) one cell: Windows' console host. The library then sends each grapheme cluster as its lead alone, the first code
   * point after canonical composition, and measures it at the lead's width, so the console and any terminal reading it count
   * the same cells: `e` and a combining accent is drawn `é`, a skin-toned emoji its emoji, a flag its first letter. 0: a
   * cluster shares the cells of its first character.
   * Measured with `ambiguous_wide`, in the same exchange. */
  unsigned char split_clusters;
  /* 1: the terminal draws a frame whole (synchronized output, DEC mode 2026), so every frame is sent between its begin and
   * end and is never seen half drawn. Asked with DECRQM in the same exchange; a terminal that does not answer is never sent
   * the mode. */
  unsigned char sync_output;
  unsigned char keyboard;       /* ROLLTUI_PROTOCOL_*: how keys arrive */
  unsigned char responsive;     /* the terminal answered a question at all; 0 for a pipe or a silent one */
  unsigned char remembered;     /* this run used remembered answers rather than asking */
  char name[64];                /* how the terminal introduced itself, for humans: "Apple_Terminal 455" */
  unsigned char images;         /* ROLLTUI_IMAGES_*: how it draws a picture */
  unsigned char images_source;  /* ROLLTUI_FACT_* */
  unsigned short cell_w, cell_h; /* a cell's size in pixels, 0 when the terminal does not say */
  /* 1: the Linux virtual console (TERM=linux, or a /dev/ttyN, or ROLLTUI_CONSOLE=1; ROLLTUI_CONSOLE=0 says it is not). Its font has
   * the PC's glyphs only, so braille, quadrants, eighths, rounded and heavy lines and a few symbols are drawn as the nearest it has,
   * and the cursor's shape is not sent. */
  unsigned char console;
  /* BEHIND TMUX, what tmux says of the terminal outside it: its name ("kitty 0.43.1", else its TERM), and whether the pane lets an
   * escape through to it (tmux's `allow-passthrough`, read and never set): 1 on or all, 0 off, -1 not known. "" and -1 elsewhere. */
  char outer[64];
  signed char passthrough;
  /* BEHIND TMUX, whether it passes keys with modifiers through (`extended-keys`, read and never set): 1 on or always, 0 off, -1 not
   * known. Off, a chord such as Ctrl-Enter or Ctrl-I apart from Tab never arrives. */
  signed char extended_keys;
} RolltuiTermFacts;

/* ONE event. `text` is a BORROW valid only for the `emit` call (rule 3): an Unknown key's raw bytes or a paste's contents; NULL otherwise.
 * `w`/`h` are set only for kind RESIZE. */
typedef struct RolltuiTermEvent {
  unsigned char kind;
  RolltuiChord key;
  RolltuiMouseEvent mouse;
  const char* text;
  size_t text_len;
  int w, h;
  unsigned char focused; /* FOCUS: 1 gained, 0 lost */
} RolltuiTermEvent;

/* Called once per event, in order. */
typedef void (*RolltuiTermEventFn)(void* ctx, const RolltuiTermEvent* e);

/* ---- lifetime --------------------------------------------------------------------------- */
/* OWNED by the caller. Enters immediately (raw mode, alt screen, the rest of `opts`) and negotiates the keyboard protocol before
 * returning. Never returns NULL: an allocation failure aborts inside rolltui::mem. */
RolltuiTerminal* rolltui_terminal_new(int in_fd, int out_fd, RolltuiTerminalOptions opts);
/* HANDS THE TERMINAL TO ANOTHER PROGRAM AND TAKES IT BACK. `suspend` leaves the alternate screen and restores the tty's own modes as
 * `free` does, freeing nothing; `resume` enters again (raw mode, alternate screen, keyboard protocol). Between the two a child (an
 * editor, a pager) may run on the same descriptors. What was on the screen is gone when it returns, so the host redraws whole
 * (`rolltui_swap_invalidate`). Both are no-ops when already in that state. */
void rolltui_terminal_suspend(RolltuiTerminal* t);
void rolltui_terminal_resume(RolltuiTerminal* t);

/* Restores the terminal (see rolltui_terminal_restore_now) and releases everything `t` holds. */
void rolltui_terminal_free(RolltuiTerminal* t);

/* ---- geometry --------------------------------------------------------------------------- */
int rolltui_terminal_is_tty(const RolltuiTerminal* t);

int rolltui_terminal_width(const RolltuiTerminal* t);

int rolltui_terminal_height(const RolltuiTerminal* t);

/* Waits up to timeout_ms (-1: forever) for input, a resize or `rolltui_terminal_wake()`; reports whatever decoded (possibly nothing). A
 * lone ESC that nothing follows within the timeout is delivered as Escape. Bytes left over from an earlier `negotiate_keyboard` or
 * `query_background` call are delivered first. */
void rolltui_terminal_poll(RolltuiTerminal* t, int timeout_ms, RolltuiTermEventFn emit, void* ctx);

/* Makes a blocked poll() return now, with whatever events are pending (possibly none). Thread-safe and async-signal-safe (one byte on the
 * self-pipe): use it instead of a short poll timeout when the view changes on another thread. */
void rolltui_terminal_wake(RolltuiTerminal* t);
/* Drops what has been typed and not yet read (a TYPEAHEAD FLUSH): a key held down over a slow action does not run on after it. */
void rolltui_terminal_flush_input(RolltuiTerminal* t);

/* Writes every byte (loops on partial writes and EINTR). */
void rolltui_terminal_write(RolltuiTerminal* t, const char* bytes, size_t len);

/* THE WINDOW'S TITLE (OSC 2), the terminal's own pushed first (XTWINOPS 22) and put back when the terminal is left or handed to a
 * program, and set again when it is taken back. Control characters are dropped; only a change is written. Inside tmux it names the
 * pane. */
void rolltui_terminal_set_title(RolltuiTerminal* t, const char* text, size_t len);
/* THE WINDOW'S NAME in tmux's list (or screen's), which the title does not set: by the escape tmux takes when its `allow-rename` is
 * on, else by `tmux rename-window`, so it is never silently refused. Leaving the terminal puts back the name the window had and its
 * `automatic-rename` (screen keeps no name to put back); coming back from a program that had the terminal sets this one again. 1 by
 * the escape, 2 by the command, 0 outside tmux and screen or before the terminal is entered (it is set then). */
int rolltui_terminal_set_window_name(RolltuiTerminal* t, const char* name, size_t len);
/* A CELL'S SIZE IN PIXELS as the host knows it (a terminal whose window size is not the truth, a font the host measured): pictures
 * are fitted to it; 0, 0 gives the terminal's own back (its report, CSI 16 t, or its window size divided by its cells). */
void rolltui_terminal_set_cell_pixels(RolltuiTerminal* t, int w, int h);
/* A RECORDING: every byte sent to the terminal from now, with when it was sent, into an asciicast (v2) file at `path`, which asciinema
 * plays and its tools turn into a GIF; a change of size is in it too. `ROLLTUI_RECORD` in the environment records any program from
 * before it enters. NULL stops it. 1 when the file is open. */
int rolltui_terminal_record(RolltuiTerminal* t, const char* path, size_t len);
/* The milliseconds until a picture held back (`picture_delay_ms`) is due to be sent, so the frame after that sends it; -1 none.
 * `rolltui_run` waits on it itself. */
int rolltui_terminal_pending_ms(const RolltuiTerminal* t);
/* AN OUTSIDE PROGRAM'S PICTURE (`rolltui_frame_outside_picture`), told to the host by `fn`:
 *   ROLLTUI_OUTSIDE_DRAW   its cells are blank on the screen and have stayed in place for `picture_delay_ms`: draw it at `screen`, the
 *                          cells in the terminal's rows and columns from 0. Told after the frame is written; the cursor is put back
 *                          when `fn` returns. Told again after anything removed it: a resize or repaint, something drawn over it
 *                          that has gone, the terminal entered again.
 *   ROLLTUI_OUTSIDE_CLEAR  it is no longer where it was drawn: gone from the frame, moved, drawn over, the screen about to be written
 *                          whole, or the terminal being left or handed to a program. Told before the frame is written, which writes
 *                          its cells again; the host removes what text does not cover (a kitty placement, a window over the terminal).
 * A stop sent from outside (Ctrl-Z in the shell) leaves from a signal handler, which tells nothing: CLEAR comes when the program
 * continues, then DRAW. `fn` may write to the terminal, itself or through a program it runs, and never presents. `user` is a BORROW
 * kept alive until the terminal is freed, which tells CLEAR for what is still drawn. NULL `fn` tells nothing. */
#define ROLLTUI_OUTSIDE_DRAW 1
#define ROLLTUI_OUTSIDE_CLEAR 2
typedef void (*RolltuiOutsidePictureFn)(void* user, int what, unsigned int id, RolltuiRect screen);
void rolltui_terminal_set_outside_pictures(RolltuiTerminal* t, RolltuiOutsidePictureFn fn, void* user);
/* MOUSE REPORTING ON OR OFF while the program runs (`mouse` and `hover` as in the options): off, the terminal selects text with the
 * pointer the way it does for a program that asks for nothing. Kept across a suspend and resume. */
void rolltui_terminal_set_mouse(RolltuiTerminal* t, int mouse, int hover);
int rolltui_terminal_mouse(const RolltuiTerminal* t);
/* THE WORKING DIRECTORY reported (OSC 7, file://host/path, percent-encoded), so a new tab or split opens there. */
void rolltui_terminal_report_cwd(RolltuiTerminal* t, const char* path, size_t len);

/* ---- background colour (light/dark auto-detect) --------------------------------------------- */
/* Asks the terminal for its background colour (OSC 11) and waits up to timeout_ms, writing it to `*out`. Returns 0 on a pipe, on no
 * answer in time, or on an unparseable answer; the caller treats every 0 as "dark". Bytes that arrive and are not the reply (a user
 * already typing) are kept and delivered by the next `rolltui_terminal_poll`. Call once, before the event loop. */
int rolltui_terminal_query_background(RolltuiTerminal* t, int timeout_ms, RolltuiStyleColorRaw* out);

/* Asks the terminal how wide it draws an East Asian AMBIGUOUS glyph: 1 for two cells, 0 for one.
 * Leaves `*out` untouched and returns 0 when the terminal does not answer, so a silent terminal
 * keeps the caller's default instead of becoming a wrong answer. Draws and erases one glyph at
 * the home position inside a save/restore pair. */
int rolltui_terminal_query_ambiguous_wide(RolltuiTerminal* t, int timeout_ms, int* out);

/* ---- what the terminal is: the facts, and drawing at the right depth ---------------------- */
/* `rolltui_terminal_new` finds these out in the same round trip that negotiates the keyboard (a fresh terminal costs one exchange, a
 * remembered one none), and a host reads them. The answers are remembered under a fingerprint of the terminal (its names and versions,
 * the operating system, ssh or a multiplexer, the program's own version), so a new terminal, release or operating system asks again, and
 * a remembered answer is re-checked in the background and corrected at the next `ROLLTUI_TERM_EVENT_FACTS` if it has gone stale.
 * Reading marks the facts as seen: a later change is what a FACTS event reports. */
void rolltui_terminal_facts(RolltuiTerminal* t, RolltuiTermFacts* out);

/* SAYS THE DEPTH OUTRIGHT, over whatever was detected: a theme file's `depth` other than "auto", or a person who knows better than the
 * environment. A ROLLTUI_DEPTH_* value, or a negative one to go back to what was detected. */
void rolltui_terminal_set_depth(RolltuiTerminal* t, int depth);

/* Diffs the swap's drawn frame, downgrades its colours to the facts' depth, and writes the bytes: `rolltui_swap_present` and
 * `rolltui_terminal_write` as ONE call whose depth cannot be forgotten. `scratch` is the caller's reusable byte buffer, cleared and left
 * holding what was written, so a frame that changed nothing allocates nothing. `rolltui_swap_present` stays for a caller with no
 * terminal (a golden test wants a depth it chose). */
void rolltui_terminal_present(RolltuiTerminal* t, RolltuiSwap* s, RolltuiStr* scratch);
/* INLINE (`RolltuiTerminalOptions::inline_rows`): writes `lines` into the terminal's own flow above the region, where they stay as
 * ordinary output and scroll into the history: a finished message, a result. Each row is written up to its last cell that is not a
 * default blank. The region is drawn again below them by the next present, which is to follow at once; on a terminal with
 * synchronized output the lines and the redrawn region reach the screen as one frame. Does nothing on the alternate screen. BORROWS
 * `lines` for the call. */
void rolltui_terminal_commit(RolltuiTerminal* t, const RolltuiFrame* lines);

/* ASKS AGAIN NOW, ignoring what was remembered, and remembers the new answers. Waits up to `timeout_ms`. 1 when the terminal answered, 0
 * when it did not (a pipe, a silent terminal); either way the facts are as good as they can be made. For a diagnostic
 * (`dirktui check-terminal`) and a host that knows the terminal changed under it. */
int rolltui_terminal_reprobe(RolltuiTerminal* t, int timeout_ms);

/* Where the remembered answers are read and written, or "" when this terminal was opened with
 * `no_cache`. A BORROW into the handle, valid until it is freed. */
const char* rolltui_terminal_cache_path(const RolltuiTerminal* t);

/* ========================================================================================
 * swap — the double buffer, the newest entry point
 * ======================================================================================== */

/* Both frames at `w` x `h`, filled with `fill`. Never returns NULL: an allocation failure
 * aborts through `rolltui_mem_alloc`, which is the library's stated answer. */
RolltuiSwap* rolltui_swap_new(int w, int h, RolltuiStyle fill);

void rolltui_swap_free(RolltuiSwap* s);

/* Resets the back frame to `w` x `h` and LENDS it for drawing. Valid until the next `begin` or `present`; the caller never frees it. A
 * size change is handled by `rolltui_render_diff` at present time. */
RolltuiFrame* rolltui_swap_begin(RolltuiSwap* s, int w, int h, RolltuiStyle fill);
/* A FRAME OF ITS OWN, for what no swap draws: the rows an inline host commits (`rolltui_terminal_commit`). OWNED by the caller, freed
 * with `rolltui_frame_free`. Never returns NULL: an allocation failure aborts inside rolltui::mem. */
RolltuiFrame* rolltui_frame_new(int w, int h, RolltuiStyle fill);
void rolltui_frame_free(RolltuiFrame* f);

/* Diffs the drawn frame against the previous one, APPENDS the bytes to `out`, and swaps.
 * After this the drawn frame is the baseline and the other is the next `begin`'s target.
 * Appends nothing when nothing changed and the cursor did not move. */
void rolltui_swap_present(RolltuiSwap* s, unsigned char depth, RolltuiStr* out);

/* "Repaint whole at the next present": THE HOST'S POLICY (a new layout, a new palette, an explicit repaint). A size change needs no call. */
void rolltui_swap_invalidate(RolltuiSwap* s);

/* The frame most recently presented, for a caller that needs to read it back — the golden
 * harness and `poll_timeout_ms` both do. Borrowed, valid until the next `present`. */
const RolltuiFrame* rolltui_swap_front(const RolltuiSwap* s);

/* ---- reading a frame back as plain text (the golden-frame harness's own use) --------------- */
/* The frame as plain text, one row per line with trailing spaces trimmed. The golden-frame
 * harness is the caller; no escapes, no styles. */
void rolltui_frame_to_text(const RolltuiFrame* f, RolltuiStr* out);

/* ========================================================================================
 * the run loop — the loop every terminal app writes, written once
 * ======================================================================================== */

/* THE LOOP EVERY TERMINAL APP WRITES, WRITTEN ONCE: enter the terminal, make the double buffer, then per frame begin, draw, present at the
 * depth the terminal has, wait for input, copy out what arrived, hand it to the app, and follow a resize and a terminal that turned out
 * not to be what was remembered; at the end put the terminal back BEFORE returning, so what an app prints afterwards lands on the
 * person's own screen. What the app cannot forget is not the app's to remember: the depth a frame is presented at; that events are
 * borrowed and must be copied before anything acts on them (handling one may resize the app or stop the loop); that a resize and a stale
 * fact repaint the whole screen; and that a terminal handed to a child program is repainted on return. An app supplies what only it
 * knows: what to draw, and what a key means.
 * ONE RUN AT A TIME PER PROCESS, on the calling thread; nothing here is thread-safe. Every callback is called on that thread, from inside
 * `rolltui_run`, with the app's `ctx` and the run (a BORROW valid until `rolltui_run` returns). `render` and `event` are required; every
 * other callback may be NULL. */
typedef struct RolltuiRun RolltuiRun;

#define ROLLTUI_RUN_STOPPED 0         /* the app called `rolltui_run_stop` */
#define ROLLTUI_RUN_NOT_A_TERMINAL 1  /* `in_fd` or `out_fd` is no terminal: nothing was drawn, nothing changed */
#define ROLLTUI_RUN_BAD_APP 2         /* `app` is NULL, or has no `render` or no `event` */

typedef struct RolltuiRunApp {
  /* The app's own state, handed back to every callback. A BORROW for the run. */
  void* ctx ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* Once, after the terminal is entered and what it is has been found out (`rolltui_run_terminal`, then
   * `rolltui_terminal_facts`), before the first frame: where an app settles its theme, its glyph width and its size. */
  void (*start)(void* ctx, RolltuiRun* run) ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* What every cell nothing is drawn on is filled with, asked before each frame so a new theme takes effect at once.
   * NULL: the terminal's own default. */
  void (*ground)(void* ctx, RolltuiRun* run, RolltuiStyle* out) ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* Draw the frame, `w` x `h` cells, at `now_ms` on a steady clock. Returns how long to wait for input: milliseconds, or -1 for as long as
   * it takes. Asked AFTER drawing because it depends on what the frame marked (a blinking cursor or a running effect does not wait).
   * `f` is lent for this call. */
  int (*render)(void* ctx, RolltuiRun* run, RolltuiFrame* f, int w, int h, unsigned long long now_ms)
      ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* One key, mouse or paste event, in the order they arrived. `e->text` (an unknown key's bytes, a paste's contents) is valid for this
   * call. Stopping here drops the rest of the batch. */
  void (*event)(void* ctx, RolltuiRun* run, const RolltuiEvent* e) ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* The terminal's size changed: this is what the next frame is drawn at. The screen is repainted whole. */
  void (*resized)(void* ctx, RolltuiRun* run, int w, int h) ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* A remembered answer about the terminal turned out to be stale (its background flipped between light and dark,
   * say): read `rolltui_terminal_facts` again and re-resolve what depends on it. The screen is repainted whole. */
  void (*facts_changed)(void* ctx, RolltuiRun* run) ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* Once per wake, after its events — and after a wake that brought none, when the wait ran out. Where an app acts on
   * what its events led to, and advances anything that moves with time. */
  void (*settle)(void* ctx, RolltuiRun* run) ROLLTUI_DEFAULT(ROLLTUI_NULL);
  /* The terminal's window gained the person's focus (1) or lost it (0). NULL: nothing is done. */
  void (*focus)(void* ctx, RolltuiRun* run, int focused) ROLLTUI_DEFAULT(ROLLTUI_NULL);
} RolltuiRunApp;

/* Runs `app` on `in_fd`/`out_fd` until it stops. `opts` is `rolltui_terminal_new`'s, and `facts_events` is always on:
 * the loop is what listens. Returns ROLLTUI_RUN_STOPPED, ROLLTUI_RUN_NOT_A_TERMINAL or ROLLTUI_RUN_BAD_APP; in every
 * case the terminal is as it was found and nothing is left allocated. */
int rolltui_run(int in_fd, int out_fd, RolltuiTerminalOptions opts, const RolltuiRunApp* app);

/* THE SAME LOOP, A STEP AT A TIME, for a host whose own loop must never block: a browser, a game engine, a toolkit's event loop.
 * `rolltui_run_begin` enters the terminal and calls `start`; NULL with `*result` set as `rolltui_run` would return it. Each
 * `rolltui_run_step` takes what has arrived without waiting (events, workers' results, due timers, `settle`), then draws and presents
 * one frame. It returns the milliseconds the frame asked to wait, sooner where a timer is due, or -1 for "until input arrives"; the
 * host steps again then, or as soon as input arrives, whichever is first. ROLLTUI_RUN_STEP_STOPPED once the app has stopped.
 * `rolltui_run_finish` leaves the terminal as it was found, frees the run and returns ROLLTUI_RUN_STOPPED. `rolltui_run_program`
 * and `rolltui_run_suspend_job` block, so a stepped host does not call them. */
#define ROLLTUI_RUN_STEP_STOPPED (-2)
RolltuiRun* rolltui_run_begin(int in_fd, int out_fd, RolltuiTerminalOptions opts, const RolltuiRunApp* app, int* result);
int rolltui_run_step(RolltuiRun* run);
int rolltui_run_finish(RolltuiRun* run);

/* A RUN WITH NO TERMINAL, for a host's own end-to-end tests: the program's real loop at `w` x `h`, drawing a screen nothing shows
 * (what it sends goes nowhere, or into a recording: `rolltui_terminal_record(rolltui_run_terminal(run), ...)`), its events the ones
 * `rolltui_run_inject` gives it: keys, the mouse, a paste and focus. Stepped as `rolltui_run_begin`'s run is; `rolltui_run_resize` gives
 * it a new size, `rolltui_run_screen_text` reads what is on screen, and `rolltui_tasks_wait_idle(rolltui_run_tasks(run), ...)` waits for
 * its workers. Its colours are 24-bit and nothing in the environment changes what it is. NULL with `*result` as `rolltui_run_begin`
 * sets it; ROLLTUI_RUN_NOT_A_TERMINAL where the system reads only a console (Windows). */
RolltuiRun* rolltui_run_begin_headless(int w, int h, const RolltuiRunApp* app, int* result);
/* A NEW SIZE for a run with no terminal, reported at the next step as a window resized is; a run on a terminal takes its window's. */
void rolltui_run_resize(RolltuiRun* run, int w, int h);
/* WHAT IS ON SCREEN after the last step, a row a line (REPLACES `*out`); 0 before the first frame. */
int rolltui_run_screen_text(const RolltuiRun* run, RolltuiStr* out);

/* WHAT THE CALLBACKS CALL, all no-ops on NULL. Leave the loop after this wake: the events of the batch that follow
 * are dropped, `settle` is still called once. */
void rolltui_run_stop(RolltuiRun* run);
/* AN EVENT THE PROGRAM MAKES (a key a macro replays, a press a script sends), copied and handed to `event` at the start of the next
 * wake, before what the terminal sent; the loop is woken for it. */
void rolltui_run_inject(RolltuiRun* run, const RolltuiEvent* e);
/* A TYPEAHEAD FLUSH: the rest of this wake's events, and what has been typed and not yet read, are dropped, so a key held down over a
 * slow action does not run on after it. */
void rolltui_run_flush_input(RolltuiRun* run);
/* The terminal the run entered, for `rolltui_terminal_facts` and the like. A BORROW valid for the run; the app never
 * frees it, and never calls `rolltui_terminal_poll` or `rolltui_terminal_present` on it — that is the loop's. */
RolltuiTerminal* rolltui_run_terminal(RolltuiRun* run);
/* STOPS THE JOB, as Ctrl-Z does in a shell: the terminal is left as it was found, the process stops, and when the shell continues it
 * (`fg`) the terminal is entered again and the screen repainted whole. A host binds it to a key of its own; a stop sent from outside
 * (`kill -TSTP`) does the same with no host code. 0 where there is no job control (Windows), and nothing happens. */
int rolltui_run_suspend_job(RolltuiRun* run);
/* Repaint the whole screen at the next frame: a keypress that asks for it, a change of palette. */
void rolltui_run_invalidate(RolltuiRun* run);
/* THE RUN'S OWN TASKS, made on first use (two workers), woken through the terminal and drained every wake, before `settle`. Freed
 * when the run ends, every outstanding `done` called first. NULL before the terminal is entered. */
RolltuiTasks* rolltui_run_tasks(RolltuiRun* run);
/* TIMERS: `fn` on the run's thread after `ms` (once), or every `ms`, fired after a wake's events and before `settle`. The wait a
 * render returns is shortened to the next one due. An id, never 0; cancelling an id that fired or never was does nothing. A
 * periodic timer that falls behind is due a period on from now, never in a burst. */
typedef void (*RolltuiTimerFn)(void* ctx, RolltuiRun* run);
unsigned int rolltui_run_after(RolltuiRun* run, int ms, RolltuiTimerFn fn, void* ctx);
unsigned int rolltui_run_every(RolltuiRun* run, int ms, RolltuiTimerFn fn, void* ctx);
void rolltui_run_cancel_timer(RolltuiRun* run, unsigned int id);
/* DEBOUNCED: the timer in `*slot` is cancelled and `fn` set `ms` from now in its place, so a burst of calls fires once, after the
 * last: filtering as a person types. */
void rolltui_run_debounce(RolltuiRun* run, unsigned int* slot, int ms, RolltuiTimerFn fn, void* ctx);

/* HAND THE TERMINAL TO A CHILD PROGRAM AND TAKE IT BACK (an editor, a pager): `suspend` leaves the alternate screen and restores the tty's
 * own modes; the child runs on the same descriptors; `resume` enters again AND repaints the whole screen, because what was on it is gone. */
void rolltui_run_suspend(RolltuiRun* run);
void rolltui_run_resume(RolltuiRun* run);

/* ========================================================================================
 * tasks — work off the UI thread, and what comes back to it
 * ======================================================================================== */

/* A FEW WORKERS, THE JOBS THEY RUN, AND THE QUEUE OF WHAT COMES BACK. A job's `work` runs on a worker; its `done`, and anything
 * posted, runs on the thread that calls `rolltui_tasks_drain`, in the order it arrived, so a host's state is only ever touched by
 * that one thread. A finished job and every post call `wake`, so a loop asleep in its input wait drains at once (`rolltui_run`
 * makes and drains its own: `rolltui_run_tasks`).
 * A JOB IS NAMED BY ITS ID: cancelling an id that has ended does nothing. A job cancelled before a worker took it never runs; one
 * cancelled while it runs finds `rolltui_job_cancelled` true, and stops early if it looks. Either way its `done` is told
 * `cancelled`, and what it made is dropped rather than shown. Every `done` is called exactly once — the place its `ctx` is freed —
 * including when the tasks are freed with jobs still queued. OWNED: `rolltui_tasks_free` cancels what is queued, waits for what is
 * running and calls every outstanding `done` before it returns. */
typedef void (*RolltuiJobWork)(void* ctx, const RolltuiJob* job);
typedef void (*RolltuiJobDone)(void* ctx, int cancelled);
typedef void (*RolltuiPostFn)(void* ctx);
/* `workers` threads (1 to 16). 0, or a system that starts none (a browser), runs each job on the thread that drains, in
 * `rolltui_tasks_drain`, one after another. `wake` is called from any thread when there is something to drain; it may be NULL. */
RolltuiTasks* rolltui_tasks_new(int workers, void (*wake)(void* ctx), void* wake_ctx);
void rolltui_tasks_free(RolltuiTasks* t); /* a no-op on NULL */
/* Queues a job; its id, never 0. */
unsigned int rolltui_tasks_start(RolltuiTasks* t, RolltuiJobWork work, RolltuiJobDone done, void* ctx);
void rolltui_tasks_cancel(RolltuiTasks* t, unsigned int id);
/* What a job's `work` asks between steps: 1 once it was cancelled. */
int rolltui_job_cancelled(const RolltuiJob* job);
/* `fn(ctx)` on the draining thread, from any thread: a worker of the host's own, a watcher, a callback. */
void rolltui_tasks_post(RolltuiTasks* t, RolltuiPostFn fn, void* ctx);
/* `fn(ctx)` on the draining thread, WAITED FOR: it returns once `fn` has run there, so a job reads what only that thread may touch
 * (the selection, a store) or hands it something. 1 when `fn` ran; 0 when the job (`job`, may be NULL from a thread of the host's
 * own) was cancelled or the tasks freed before it ran, and then it never runs. From the draining thread itself it runs `fn` at once. */
int rolltui_tasks_call(RolltuiTasks* t, const RolltuiJob* job, RolltuiPostFn fn, void* ctx);
/* A JOB ASKS THE PERSON and waits for the answer: `ask(ctx, question)` runs on the draining thread to put the question in front of
 * them (a prompt, a confirm), and what takes the answer calls `rolltui_tasks_answer` with that `question`, from any thread. The job is
 * paused meanwhile and carries on with the answer's text in `*out` (may be NULL): 1 for an answer given, 0 for a refusal (`yes` 0: No,
 * Escape), -1 when the job was cancelled or the tasks freed first. Never from the draining thread, which would wait on itself: -1. */
typedef void (*RolltuiAskFn)(void* ctx, unsigned int question);
int rolltui_job_ask(RolltuiTasks* t, const RolltuiJob* job, RolltuiAskFn ask, void* ctx, RolltuiStr* out);
/* The answer to `question`: 1 when a job was waiting on it, 0 when none is (it was cancelled), so the host closes its question. */
int rolltui_tasks_answer(RolltuiTasks* t, unsigned int question, const char* text, size_t len, int yes);
/* The job `id` started and its `done` not yet called. */
int rolltui_tasks_running(RolltuiTasks* t, unsigned int id);
/* Runs what has come back, on this thread; how many callbacks ran. */
size_t rolltui_tasks_drain(RolltuiTasks* t);
/* Jobs started whose `done` has not been called yet: busy while it is not 0. */
size_t rolltui_tasks_busy(RolltuiTasks* t);
/* Drains until nothing is busy, or `timeout_ms` passes (-1: no limit); 1 when idle. What a test waits on. */
int rolltui_tasks_wait_idle(RolltuiTasks* t, int timeout_ms);

/* ========================================================================================
 * processes — other programs, run from here, and what they say
 * ======================================================================================== */

/* A PROGRAM THIS ONE STARTED, read and written without waiting: a previewer, `git`, a build, a shell command. `argv[0]` is found on
 * PATH (and, on Windows, with PATHEXT's extensions; a script runs through cmd.exe, which refuses an argument holding `%` or `"`).
 *   PIPES  stdin a pipe the host writes; stdout and stderr one stream the host reads. The program has its own process group, so
 *          stopping it stops what it started.
 *   PTY    a pseudo-terminal `cols` x `rows`, so the program writes colour and progress as it would to a terminal; resized with
 *          `rolltui_child_resize`. On Windows it is pipes with COLUMNS and LINES set, because ConPTY reorders what a program wrote.
 *   QUIET  the null device for all three streams, and its own session: Ctrl-C here never reaches it, and freeing the handle lets it
 *          go rather than stopping it (a background check, a viewer launched for a file).
 * OWNED: `rolltui_child_free` stops a PIPES or PTY program still running. */
typedef struct RolltuiChild RolltuiChild;
#define ROLLTUI_CHILD_PIPES 0
#define ROLLTUI_CHILD_PTY 1
#define ROLLTUI_CHILD_QUIET 2
typedef struct RolltuiChildOptions {
  const char* const* argv ROLLTUI_DEFAULT(ROLLTUI_NULL); /* NULL-terminated; BORROWED for the call */
  const char* cwd ROLLTUI_DEFAULT(ROLLTUI_NULL);         /* NULL: this program's */
  const char* const* env ROLLTUI_DEFAULT(ROLLTUI_NULL);  /* "NAME=value" entries over this program's environment, NULL-terminated */
  unsigned char mode ROLLTUI_DEFAULT(ROLLTUI_CHILD_PIPES);
  int cols ROLLTUI_DEFAULT(80), rows ROLLTUI_DEFAULT(24); /* PTY */
} RolltuiChildOptions;
/* NULL when it could not start, with the reason in `*err` (may be NULL): "not found: bat", "git: Permission denied". */
RolltuiChild* rolltui_child_spawn(const RolltuiChildOptions* o, RolltuiStr* err);
void rolltui_child_free(RolltuiChild* c); /* a no-op on NULL */
/* What the program wrote since the last read, without waiting: up to `cap` bytes, 0 when nothing has arrived. */
size_t rolltui_child_read(RolltuiChild* c, char* buf, size_t cap);
/* READS ONLY WHAT A VIEW SHOWS: everything that has arrived, fed to `t`, stopping once `t` holds `max_lines` lines (0: no stop). A
 * program writing more waits on its full pipe, and the rest is read when the view asks for more lines. Returns the bytes read. */
size_t rolltui_child_pump(RolltuiChild* c, RolltuiAnsiText* t, size_t max_lines);
/* 1 once the program has closed its output and everything it wrote has been read. */
int rolltui_child_drained(const RolltuiChild* c);
/* Writes all of `bytes` to the program's input (it waits while the pipe is full); 0 when the program is gone. */
int rolltui_child_write(RolltuiChild* c, const char* bytes, size_t len);
/* The end of the program's input: a pipe closed, a pty's ^D. */
void rolltui_child_close_input(RolltuiChild* c);
void rolltui_child_resize(RolltuiChild* c, int cols, int rows);
/* 1 once the program has ended, asked without waiting; `*status` (may be NULL) its exit status, 128 + the signal that ended it. */
int rolltui_child_done(RolltuiChild* c, int* status);
/* Waits until the program has ended (its output is not read meanwhile: a PIPES or PTY program that fills its pipe waits too, so read
 * what it writes first). 1 when it ended, `*status` as `rolltui_child_done` gives it. */
int rolltui_child_wait(RolltuiChild* c, int* status);
/* Ends it: SIGTERM to its group, and SIGKILL 200 ms later if it is still there (TerminateProcess on Windows). */
void rolltui_child_stop(RolltuiChild* c);
/* How long a render should wait while a program is running and its output is wanted: a short interval, so what it writes is drawn
 * as it arrives. -1 once it is done and drained. */
int rolltui_child_wait_ms(const RolltuiChild* c);
/* A PROGRAM RUN INTO AN `output:<name>` WINDOW: started now as `o` says (ROLLTUI_CHILD_PTY for a program's colours), its output the
 * window's text, read as the window draws, and with a pseudo-terminal told the window's size as it changes. It is stopped when a
 * window that showed it leaves the layout, when the name is bound to something else, or with the table. Several run into one name
 * are merged line by line, each line led by `label` (may be NULL) in `role`. The window's footer says how it is doing unless options
 * were set. 1 when it started; 0 with the reason in `*err` (may be NULL). */
int rolltui_windows_run_output(RolltuiWindows* w, const char* name, size_t len, const RolltuiChildOptions* o, const char* label,
                               unsigned char role, RolltuiStr* err);
/* 1 while a program run into `name` is running; `*status` (may be NULL) the exit status of the last to end, -1 for one stopped. */
int rolltui_windows_output_running(RolltuiWindows* w, const char* name, size_t len, int* status);
/* A rule across an output window's text after what is there, `label` on it (a build started, a test run's boundary). */
void rolltui_windows_output_mark(RolltuiWindows* w, const char* name, size_t len, const char* label, size_t label_len);

/* A FILE OR A URL opened with what the system opens it with (`open`, `xdg-open`, the shell's ShellExecuteW), which returns at once.
 * 1 when it was handed over; 0 with the reason in `*err` (may be NULL). */
int rolltui_open_with_system(const char* target, size_t len, RolltuiStr* err);

/* THE CLIPBOARD set to `text`, two ways at once: OSC 52 through `t` (the terminal sets it, the one way that reaches the person's own
 * machine over ssh; inside tmux it is passed through; not sent above 74994 encoded bytes, the most terminals take), and the first
 * native tool that works (pbcopy; wl-copy, xclip or xsel; the Windows clipboard), or the command `$ROLLTUI_CLIPBOARD` names, which is
 * fed the text. 1 when either took it. `t` may be NULL: the native tool alone. */
int rolltui_clipboard_copy(RolltuiTerminal* t, const char* text, size_t len);
/* THE CLIPBOARD READ: the first native tool that answers (pbpaste; wl-paste, xclip or xsel; the Windows clipboard), or the command
 * `$ROLLTUI_CLIPBOARD_PASTE` names; where none can (over ssh with no display), what this program last copied through `t`. 1 with
 * the text in `*out` (REPLACED); 0 when there is nothing to give. */
int rolltui_clipboard_paste(RolltuiTerminal* t, RolltuiStr* out);

/* THE CONTROLLING TERMINAL by name (/dev/tty; the console's CONIN$ and CONOUT$), whatever stdin and stdout are: for a program whose
 * stdout is its answer to a shell (a file chooser, cd-on-exit), which draws here and prints its answer there. 1 with the descriptors
 * to hand `rolltui_run`; `rolltui_close_terminal` closes them. */
int rolltui_open_terminal(int* in_fd, int* out_fd);
void rolltui_close_terminal(int in_fd, int out_fd);

/* A PROGRAM THAT TAKES THE TERMINAL (an editor, a pager), run from inside `rolltui_run`: the terminal is handed to it, this process
 * waits while it runs (ignoring the Ctrl-C it delivers to the program), and the terminal is taken back and the screen repainted.
 * 1 when it started; `*status` (may be NULL) its exit status; 0 with the reason in `*err` (may be NULL). */
int rolltui_run_program(RolltuiRun* run, const char* const* argv, const char* cwd, int* status, RolltuiStr* err);
/* THE STORES' FILES WATCHED while the run lasts: a theme, layout or key file another program changes (the studio, an editor) is read
 * again (`rolltui_theme_store_refresh` and the others) and the screen drawn again, and SIGUSR1 reads them all again. Each store's
 * version moves as an edit's would, so an app that follows its stores' versions takes the change. NULL for a store not watched;
 * BORROWED, kept by the host for the run. Calling it again replaces what is watched. */
void rolltui_run_watch_stores(RolltuiRun* run, RolltuiThemeStore* theme, RolltuiLayoutStore* layout, RolltuiBindingsStore* keys);

/* ========================================================================================
 * RELEASE — and the number that proves you did
 * There is no RAII in C: create and release in a pair, then `rolltui_shutdown()` and assert
 * `live_bytes == 0`. That is how a missed release is caught rather than hoped about.
 * ======================================================================================== */

/* Releases everything the library retains: every registered process-wide releaser (most recently registered first), then the CALLING
 * thread's scratch via `rolltui_release_thread` (another thread's `_Thread_local` storage cannot be freed from here). Safe to never call
 * and safe to call twice. Nothing is invalidated for a host that carries on: the caches rebuild on next use. */
void rolltui_shutdown(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_H */

