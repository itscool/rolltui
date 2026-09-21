#ifndef ROLLTUI_H
#define ROLLTUI_H
/*
 * rolltui.h — the library's one public header: every public type is defined here, every public function declared here, and the
 * vocabulary those signatures speak (roles, keys, caps, enums) lives here. `#include "rolltui/rolltui.h"` is the whole API.
 *
 * The headers under `rolltui/c/` are internal: they include this file first and add what the library's own `.c` files need. Don't
 * include one from a host; a suite that tests an internal names it and is listed in CMake's opt-in list (`public_header_test`
 * asserts no other consumer does). What is public, and why, is `tests/api_classes.inc` (one class per function, held to measured
 * reach); who each function is for is `tests/api_roles.inc`, and the three parts below follow it.
 *
 * LAYOUT. PART 1 is the nouns: every type, table and name (C needs a type before the functions that take it). PART 2 is the host
 * author's verbs: load, bind, run, release. PART 3 is the widget author's: what implementing a kind needs.
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
 * PART 1 — THE NOUNS: every type, table and name the two roles speak
 * C needs a type before the functions that take it, so every public type is defined here; the verbs are in Parts 2 and 3.
 * Shared by a host and a widget author: `RolltuiStr`, `RolltuiRect`, `RolltuiStyle`, `RolltuiEvent`, `RolltuiFrame`, with their
 * operations declared beside them. A name typed into a FILE is an interface too, so a table that is one says so and names its
 * shipped directory.
 *
 * Who authors which file. A THEME or BINDINGS file is a user's: neither can break a host (an unknown role is reported and ignored;
 * a chord bound to an undeclared action is kept and inert). A LAYOUT is the app's to ship: host code names its windows, a user
 * selects among the layouts an app ships, and authoring one is a developer act (`rolltui-studio`). A MENU is the app's too, because
 * nobody has asked for more: the user-shadowing rung works but is not promised.
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
 * (the C++ destructor does). `_add` appends a copy and returns a BORROW of the stored entry, valid until the next `_add`. */
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

/* ---- the key vocabulary, in BOTH its spellings ---------------------------------------------
 * One list, three columns: the C constant's suffix, the TitleCase display name (what `to_string(Event)` prints), and the lowercase
 * file name (what a bindings file spells; "" for `Char` and `Unknown`, which have no file word). Adding a key is one edit here.
 * ORDER IS ABI, same as `ROLLTUI_ROLE_LIST`: the ordinal crosses in every `RolltuiChord`. */
/* The lowercase words are what a BINDINGS FILE spells ("ctrl+k", "alt+left", "f2"); a chord naming none of them is a bad value with a
 * reason, never a silent miss. Shipped files: `rolltui/presets/bindings/`. */
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

/* One chord: a key, the character it is when the key is CHAR, and the three modifiers (never the raw bytes). */
typedef struct RolltuiChord {
  unsigned char key ROLLTUI_DEFAULT(ROLLTUI_KEY_CHAR);
  RolltuiCodepoint ch ROLLTUI_DEFAULT(0);
  unsigned char ctrl ROLLTUI_DEFAULT(0), alt ROLLTUI_DEFAULT(0), shift ROLLTUI_DEFAULT(0);
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
  int button ROLLTUI_DEFAULT(0);                  /* 1 left, 2 middle, 3 right; 0 for motion/wheel */
  unsigned char ctrl ROLLTUI_DEFAULT(0), alt ROLLTUI_DEFAULT(0), shift ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  constexpr bool operator==(const RolltuiMouseEvent&) const = default;
#endif
} RolltuiMouseEvent;

#define ROLLTUI_EVENT_KEY 0

#define ROLLTUI_EVENT_MOUSE 1

#define ROLLTUI_EVENT_PASTE 2

/* ONE event. `text` is a BORROW valid only for the `emit` call: an Unknown key's raw bytes or a paste's contents; NULL otherwise.
 * A resize has no kind here: the Terminal makes it. */
typedef struct RolltuiEvent {
  unsigned char kind;
  RolltuiChord key;
  RolltuiMouseEvent mouse;
  const char* text;
  size_t text_len;
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
 * bindings — a host holds a bindings table and its report
 * ======================================================================================== */

/* The canonical spelling and the help form; "" for an Unknown key. Thirty-two is past the longest either produces. */
#define ROLLTUI_CHORD_STRING_MAX 32

/* ---- the table --------------------------------------------------------------------------- */
/* OWNED, LONG-LIVED: one per `rolltui::Bindings`, which frees it. Three parallel lists: the DECLARED actions with their descriptions,
 * and the ROWS (action -> chords). They are separate because a row may outlive a declaration: a bindings file is global and the
 * user's, so a row for another screen's action is KEPT and inert. */
typedef struct RolltuiBindings RolltuiBindings;

/* Answers whether a scope is one the library defines — the caller's fact, asked for by
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

/* ========================================================================================
 * style — RolltuiStyle and the role list
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
// Declared, not defined: an opaque enum declaration with a fixed underlying type is a complete type, which is all a member needs.
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
  // COPY IS DELETED: spell it `clone()` (`rolltui_doc_entry_copy`). Move and the destructor stay.
  RolltuiDocEntry(const RolltuiDocEntry&) = delete;
  RolltuiDocEntry(RolltuiDocEntry&& o) noexcept;
  RolltuiDocEntry& operator=(const RolltuiDocEntry&) = delete;
  RolltuiDocEntry& operator=(RolltuiDocEntry&& o) noexcept;
  ~RolltuiDocEntry();
  RolltuiDocEntry clone() const;
#endif
} RolltuiDocEntry;

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
  RolltuiDocument(const RolltuiDocument&) = delete;  /* clone() is the spelling */
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
  /* Trims or grows, KEEPING the storage past the end: a transcript that trims and refills wants the entries back. */
  void resize(std::size_t k);
#endif
} RolltuiDocument;

/* ---- forward declarations the C++ members just below call ----------------------------------*/
void rolltui_doc_entry_copy(RolltuiDocEntry* to, const RolltuiDocEntry* from);
void rolltui_doc_entry_release(RolltuiDocEntry* e);
RolltuiDocEntry* rolltui_document_add(RolltuiDocument* d);
void rolltui_document_clear(RolltuiDocument* d);
void rolltui_document_copy(RolltuiDocument* to, const RolltuiDocument* from);
void rolltui_document_release(RolltuiDocument* d);

#ifdef __cplusplus
/* ---- the C++ special members of the structs above -----------------------------------------
 * Each calls a C function declared above, so "release this subtree" has one implementation; `inline`, beside the declarations,
 * keeps the library free of any C++ translation unit. */
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

/* One cell: a grapheme cluster, its style, its width and its hyperlink id. A cluster has no maximum length: ten bytes cover ASCII,
 * accented Latin, CJK, an emoji with a variation selector or skin tone, and a flag; anything longer (a ZWJ family is 25+ bytes)
 * SPILLS into a table the frame owns, its index kept where the bytes would be (`len` reads ROLLTUI_CELL_SPILLED), the same shape
 * `link` uses. */
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

/* THE CELL HAS NO PADDING, asserted: `rolltui_frame_equal` compares cells with `memcmp`, which is only right when every byte was written. */
ROLLTUI_STATIC_ASSERT(sizeof(RolltuiCell) == 4 + 15 + ROLLTUI_CELL_INLINE_GLYPH + 3,
                      "RolltuiCell has padding; memcmp equality would compare bytes nobody wrote");

typedef struct RolltuiFrame RolltuiFrame;

/* Reuses every buffer it can: the cells, the link table's strings and the
 * spill table's. A steady frame allocates nothing through here. */

/* ---- geometry and cells ----------------------------------------------------------- */

/* A COPY of the cell into `out`, which the caller owns. Out of bounds writes a zeroed cell with width 0. (A caller's buffer and not
 * a return value: a non-POD class returned from `extern "C"` has no ABI every compiler promises.) */

/* ---- marks (the widget's whole vocabulary for motion) ------------------------------ */

/* ========================================================================================
 * frame_ops — drawing into the cell grid
 * ======================================================================================== */

typedef struct RolltuiDrawScratch RolltuiDrawScratch;

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
/* `rolltui::InputOptions` IS this struct. The two strings are OWNED `RolltuiStr`s; a copy is a named function. */
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
  /* ONE ROW: a newline is dropped, the text never wraps, and the row slides under the caret
   * — a menu field, an address bar. Off: the text wraps and the window grows with it. */
  unsigned char single_line ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiInputOptions();
  bool operator==(const RolltuiInputOptions& o) const;
#endif
} RolltuiInputOptions;

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

/* THE HOST'S CLIPBOARD: a function pointer and a context. Set once; NULL turns it off. The text is a BORROW for the call. */
typedef void (*RolltuiCopyFn)(void* ctx, const char* text, size_t len);

/* The thirty action names, in command order. The input knows what each command DOES and none of the words. */

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

/* THE FOUR ROLES A DRAW NEEDS, handed in as bytes. `prompt` is the OPTIONS' role, which is
 * why it is not in here. */

typedef struct RolltuiInputRoles {
  unsigned char text;
  unsigned char selection;
  unsigned char placeholder;
} RolltuiInputRoles;

#ifdef __cplusplus
/* `RolltuiInputOptions`' one special member: the default prompt, a value the struct must start with. Inline here because a body
 * inside the struct cannot yet see `rolltui_str_set`. */
inline RolltuiInputOptions::RolltuiInputOptions() { rolltui_str_set(&prompt, "> ", 2); }

#endif

/* ========================================================================================
 * md_lines — the transcript's line store
 * ======================================================================================== */

typedef struct RolltuiMdLines RolltuiMdLines;

/* ---- working memory the store lends its filler ------------------------------------------
 * Both renderers cluster text constantly; this is the buffer they do it in, one per store, so a renderer holds no per-thread state. */
typedef struct RolltuiUnicodeScratch RolltuiUnicodeScratch;

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
  RolltuiInputSpec clone() const;  /* `rolltui_input_spec_copy` */
#endif
} RolltuiInputSpec;

struct RolltuiMenuItem;

/* PINNED PUBLIC BY A PUBLIC STRUCT'S C++ MEMBERS: `RolltuiMenuItem` holds a `RolltuiInputSpec` by value, and its `clone()` and
 * `operator==` call these. */
void rolltui_input_spec_copy(RolltuiInputSpec* to, const RolltuiInputSpec* from);
int rolltui_input_spec_equal(const RolltuiInputSpec* a, const RolltuiInputSpec* b);

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
  RolltuiStr value;       /* Choice: the current option id; Input: the COMMITTED text */
  RolltuiInputSpec spec;  /* Input: the type and its constraints */
  RolltuiMenuItemList children; /* Submenu: items; Choice: options */

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
  static RolltuiMenuItem input(const char* id, const char* label, RolltuiInputSpec spec, const char* value = "");
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

#ifdef __cplusplus
/* ---- the C++ special members of the structs above -----------------------------------------
 * Each calls a C function declared above, so "release this subtree" has one implementation; `inline`, beside the declarations,
 * keeps the library free of any C++ translation unit. */
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

#ifdef __cplusplus
  // A kind says what it wants drawn. Records the full length, so an override past the cap is refused by the applier rather than
  // truncated into a narrower glyph.
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

#ifdef __cplusplus
  std::size_t roles_size() const { return role_count; }
  unsigned char role(std::size_t i) const { return roles[i % role_count]; }
#endif
} RolltuiEffectSpec;

/* ---- what a THEME carries, owned in C -------------------------------------------------- */
/* A map of STATE -> the specs that state looks like while it lasts. `states` is the caller's state vocabulary size (the C never learns
 * a state's name); `fallback_role` is the role a spec with none of its own picks. OWNED, LONG-LIVED: one map per `rolltui::Theme`,
 * which frees it. */
typedef struct RolltuiEffectMap RolltuiEffectMap;

/* A BORROW, valid until the map next changes. NULL for an index past the state's specs. */

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
 * theme — a host holds a style table
 * ======================================================================================== */

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

/* THE SETTING layer: what a preset file and `--color-depth` / `--theme-mode` accept: every name above PLUS "auto" (resolve it, do not
 * store it). A host may narrow what IT accepts through the parse callbacks. */
int rolltui_color_depth_setting_valid(const char* s, size_t len);

int rolltui_theme_mode_setting_valid(const char* s, size_t len);

/* The colour in the same spelling. "#rrggbb" is the longest, so seven bytes plus nothing —
 * the result is NOT terminated and the length is returned. */
#define ROLLTUI_COLOR_STRING_MAX 8

/* A COLOUR, BOTH WAYS. A typed `color` input field (`"kind": "input", "type": "color"`) hands the host back TEXT, so the round trip
 * is public. `parse`: "#rrggbb" | "none" | "0".."255"; 1 on success, 0 when it is not a colour. `to_string`: the same spelling back
 * into `cap` bytes (`ROLLTUI_COLOR_STRING_MAX` is enough); NOT terminated, the length is returned.
 * What is constrained: an EFFECT may never invent a colour (it picks the base style or a ROLE the theme named), which keeps `mono`
 * legible; a WIDGET drawing may write any colour into any cell (`rolltui_frame_put_text` and `_fill` take a style by value), and the
 * renderer down-converts at the frame's depth. */
int rolltui_color_parse(const char* text, size_t len, RolltuiStyleColor* out);
/* A style faded TOWARD a ground colour: `keep` 1 is the style itself, 0 is the ground. Only RGB
 * colours fade; a palette colour stays as it is. A fading status note, a column clipped at an edge. */
void rolltui_style_fade(const RolltuiStyle* st, RolltuiStyleColor ground, double keep, RolltuiStyle* out);

/* A style ON a ground: `fg`/`bg` "none" (kind 0) become `ground`; a stated colour is left as the theme wrote it. That is what "none"
 * in a theme file MEANS for a role that sits on whatever it is drawn over. Mutates in place; `ground` is typically the caller's
 * already-resolved background. */
void rolltui_style_on(RolltuiStyle* st, RolltuiStyleColor ground);

size_t rolltui_color_to_string(RolltuiStyleColor c, char* out, size_t cap);

/* The SGR sequence that selects `style` at `depth`, into `out`. Always starts from a reset, so a cell's style never depends on the
 * previous cell's. The longest is 48 bytes; the cap is a constraint on this file, not on a caller's data. */
#define ROLLTUI_SGR_MAX 64

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
/* THE GROWING BUFFERS THESE ALGORITHMS NEED, owned by the caller and reused across calls: the functions marked below need somewhere to
 * decode into, mark boundaries in and build line-break units in. One handle per thread, made once; after the first few calls it never
 * grows, so the draw path allocates nothing. One buffer per ROLE, so a function that calls another (graphemes -> boundaries,
 * display_width -> graphemes) cannot alias its own scratch. */
typedef struct RolltuiUnicodeScratch RolltuiUnicodeScratch;

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

typedef struct RolltuiMenu RolltuiMenu;

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
  /* …and the INPUT widget's own thirty, because a typed field forwards every key it does not claim to the editor. */
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

typedef struct RolltuiMenuOptions {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  int inset ROLLTUI_DEFAULT(0); /* columns kept clear on each side of the area */
#ifdef __cplusplus
  bool operator==(const RolltuiMenuOptions&) const = default;
#endif
} RolltuiMenuOptions;

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
  /* A row that carries a value is two things: `Ink: #d8dce2` and `Shading  ascii` are a name and an answer, so they draw in two styles.
   * Only the FOREGROUND is taken: the row keeps its own background, so a selected row stays one solid block. */
  unsigned char label;
  unsigned char value;
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

/* ---- forward declarations the C++ members just below call ----------------------------------*/
void rolltui_menu_action_list_release(RolltuiMenuActionList* l);

/* Serialises `root`, 2-space indented with a trailing newline. REPLACES `*out`. */

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
#ifdef __cplusplus
  bool operator==(const RolltuiTextPos&) const = default;
#endif
} RolltuiTextPos;

/* ---- forward declarations the C++ members just below call ----------------------------------*/
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

/* ========================================================================================
 * layout_tree — the tree inside RolltuiLayout: a public type, so its lifecycle is public
 * ======================================================================================== */

#ifdef __cplusplus
namespace rolltui {
enum class Border : unsigned char { None, Single, Rounded, Double, Heavy };
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
  /* Columns kept clear of the screen's left and right edges when `clamp` fits this popup on screen: 0 (the default) clamps flush to the
   * edge, as a docked editor panel wants; a popup that centres over the whole screen wants a gutter so it never grows (via `min_w`) to
   * either edge. Width only. */
  unsigned char edge_margin ROLLTUI_DEFAULT(0);
  RolltuiOptDim min_w, min_h, max_w, max_h;
#ifdef __cplusplus
  bool operator==(const RolltuiPlacement&) const = default;
#endif
} RolltuiPlacement;

struct RolltuiLayoutNode;

/* A node's OWNED children. Pointers, not values, so a child's address never moves and a `const RolltuiLayoutNode*` handed out by
 * `place()` or `find()` stays valid across an edit. */
/* ---- THE LAYOUT FAMILY IS OPAQUE ------------------------------------------------------------
 * A layout, its layers, the nodes of its split tree and the three lists that hold them are HANDLES here and structures in
 * `rolltui/c/rolltui_layout_tree.h` (the library's own). Opaque now with a door opened later is reversible; transparent now and
 * opaque later is a break. The eight doors declared in PART 2 are the whole of what consumers do with a layout: hand the base to a
 * window stack, declare the actions, show the name and minimum size, and read an id off a node or layer the library handed back. If
 * a real need appears, add a door and say who forced it. The one consumer that walks and mutates a tree is the studio's layout
 * editor, which reaches the structures through the internal header by name. */
typedef struct RolltuiNodeList RolltuiNodeList;
typedef struct RolltuiLayoutNode RolltuiLayoutNode;
typedef struct RolltuiLayer RolltuiLayer;
typedef struct RolltuiLayerList RolltuiLayerList;
typedef struct RolltuiContent RolltuiContent;
typedef struct RolltuiActionList RolltuiActionList;
typedef struct RolltuiLayout RolltuiLayout;

/* The eight doors are declared in PART 2 (a host's own section), where their roles put them. */

/* Appends an EMPTY child and returns it — the C's `emplace_back`, so a caller never builds a
 * node on the stack and copies it in. */

/* ---- popups: an OWNED, growable array of Layer VALUES -------------------------------------- */

/* PLAIN DATA, emitted per node per frame: it borrows the node it describes and is what a host reads to draw. `node` is a BORROW valid
 * while the tree it came from is not edited. */
typedef struct RolltuiResolvedNode {
  const RolltuiLayoutNode* node ROLLTUI_DEFAULT(nullptr);
  RolltuiRect outer; /* the node's box before clipping to the frame */
  RolltuiRect inner; /* outer minus the border, clipped — what a split divides / a slot draws in */
  unsigned char focused ROLLTUI_DEFAULT(0);
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
} RolltuiLayoutRoles;

/* The host fills a window's content slot into `rn->inner` (already clipped). NULL draws
 * nothing, which is what a golden-frame harness wants. */
typedef void (*RolltuiSlotFn)(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f);

/* WORKING MEMORY THE CALLER OWNS: two screen-sized arm maps (the joins written so far, and what was under the ring before this window
 * cleared it) plus the draw scratch a title needs. One buffer per ROLE, so the compose's maps and the text walk's clusters cannot alias. */
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

/* A SESSION's registries and caches — see "THE SESSION" in Part 2 for the six-point contract.
 * Opaque: a host makes one, hands it to what needs it, and frees it. */
typedef struct RolltuiContext RolltuiContext;

typedef struct RolltuiWindowStack RolltuiWindowStack;

/* THE THREE ACTION NAMES, handed in — this file knows the RULES and none of the words. */
typedef struct RolltuiStackActions {
  const char* close_popup;
  const char* focus_next;
  const char* focus_prev;
} RolltuiStackActions;

#define ROLLTUI_ROUTE_DELIVER 0

/* The close key on a popup — or a press outside one whose layout says `"dismiss": true`. */
#define ROLLTUI_ROUTE_CLOSED_POPUP 1

#define ROLLTUI_ROUTE_FOCUS_MOVED 2

#define ROLLTUI_ROUTE_DROPPED 3
/* The close key, or a press outside a dismissing popup, closed ONE LEVEL inside the focused
 * window's widget (a dropdown, a menu level) and the popup stays; `window` names that window. */
#define ROLLTUI_ROUTE_CLOSED_LEVEL 4

/* WHO ANSWERS "CLOSE ONE LEVEL" — the shape; the setter is with the stack's other setup. */
typedef int (*RolltuiStackLevelFn)(void* ctx, const char* window, size_t len);

/* The window a press captured the pointer for, until its release ("" when none). */

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
} RolltuiWidgetPlugin;

/* One widget: what it IS and how to talk to it. The plugin is a BORROW of a table the
 * implementor keeps (a `static const` per kind); `ctx` is OWNED by whoever holds this. */
typedef struct RolltuiWidget {
  const RolltuiWidgetPlugin* vt;
  void* ctx;
} RolltuiWidget;

typedef struct RolltuiWindows RolltuiWindows;

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
  RolltuiStyleColor swatch;
  unsigned char has_swatch;
} RolltuiRow;

/* ---- forward declarations the C++ members just below call ----------------------------------*/
void rolltui_rows_add(RolltuiRows* r, const char* label, size_t label_len, const char* value, size_t value_len);
void rolltui_rows_add_colour(RolltuiRows* r, const char* label, size_t label_len, RolltuiStyleColor colour);
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
  void add_colour(const char* label, RolltuiStyleColor colour) { rolltui_rows_add_colour(this, label, std::strlen(label), colour); }
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
 * a bare string converts to "no motion". `rolltui::Note` IS this struct. */
typedef struct RolltuiNote {
  RolltuiStr text;
#ifdef __cplusplus
  rolltui::EffectState state = static_cast<rolltui::EffectState>(0); /* None */
#else
  unsigned char state;
#endif
  unsigned long long since_ms ROLLTUI_DEFAULT(0); /* when it entered `state` */
#ifdef __cplusplus
  // No constructor, destructor or assignment beyond the converting ones below: `text` (a `RolltuiStr`) already has correct copy, move
  // and destroy.
  RolltuiNote() = default;
  // Implicit from a C string on purpose: a host with no motion to report writes `return "working";`. Anything else sets the text by
  // pointer and length.
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

/* note: `out` is the caller's `RolltuiNote`, already cleared, filled in place. */
typedef void (*RolltuiNoteFn)(void* ctx, RolltuiNote* out);

typedef struct RolltuiWidgetEnv {
  unsigned char ambiguous_wide ROLLTUI_DEFAULT(0);
  unsigned long long now_ms ROLLTUI_DEFAULT(0);
} RolltuiWidgetEnv;

/* THE TWO ROLES THE WINDOW ITSELF DRAWS WITH: the scrollbar's track and thumb, which live in the window's border column and which a
 * widget never sees. */
typedef struct RolltuiWindowRoles {
  unsigned char scrollbar;
  unsigned char border;
  unsigned char border_active;
} RolltuiWindowRoles;

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

/* ========================================================================================
 * marker — the "N more" rule
 * ======================================================================================== */

/* It SHORTENS rather than eating the line: the marker writes over CONTENT cells, and at 20 cells wide the full form took most of the
 * row. That is safe because the scrollbar carries the proportion; the bar is the positional signal and the marker the NON-GRAPHICAL one,
 * which a mono theme, a low colour depth or a borderless window still has. Writes 0 bytes when there is nothing below or no room.
 * `out` needs ROLLTUI_MARKER_MAX. */
#define ROLLTUI_MARKER_MAX 40

/* ========================================================================================
 * mem — the leak gauge — public by stated reason: it is how a consumer proves it released everything
 * ======================================================================================== */

/* ========================================================================================
 * presets — the preset stores and domains
 * ======================================================================================== */

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
  /* A report of THIS domain's type, made and unmade by the domain: OWNED, short-lived. It lets the mechanics parse into a report of their
   * own instead of asking every caller for a second one. */
  void* (*create)(void);
  void (*destroy)(void* report);
} RolltuiPresetReportFns;

/* One domain: its four names, its embedded shipped table, and what can be done to a value. `cache` is OWNED by this descriptor and built
 * on first use (see `rolltui_preset_domain_release`). */
typedef struct RolltuiPresetShippedCache RolltuiPresetShippedCache;

/* A pure predicate over a "mode"/"depth" string, with no context to capture. */
typedef int (*RolltuiThemePresetValidFn)(const char* s, size_t len);

typedef struct RolltuiPresetDomain {
  const char* kind;         /* "theme" | "layout" | "bindings" — for messages */
  size_t kind_len;
  const char* working_file; /* "theme.working.json" */
  size_t working_file_len;
  const char* subdir;       /* "themes" */
  size_t subdir_len;

  size_t (*shipped_count)(void);
  void (*shipped_at)(size_t i, const char** name, size_t* name_len, const char** text, size_t* text_len);

  /* TEXT in, an OWNED value out (NULL on failure, with the report saying why). The JSON never crosses this boundary. */
  void* (*parse)(const struct RolltuiPresetDomain* d, const char* text, size_t len, void* report);
  /* A PARTIAL file (a colours-only theme file): fills part of `working`, notes why; NULL
   * when the file is not partial, and `parse` is then used. */
  void* (*parse_partial)(const struct RolltuiPresetDomain* d, const char* text, size_t len, const void* working,
                         void* report);
  void (*to_json)(const struct RolltuiPresetDomain* d, const void* value, const char* name, size_t name_len,
                  RolltuiPutFn put, void* ctx);
  /* …and the same, with the working copy's ORIGIN written into it as "preset". One call
   * rather than a second serialiser, because that key is the store's and not the domain's. */
  void (*to_json_with_origin)(const struct RolltuiPresetDomain* d, const void* value, const char* name,
                              size_t name_len, RolltuiPutFn put, void* ctx);
  /* …and back: the preset name a working-copy file says it came from, "default" when it says nothing. A callback for the same reason as
   * its pair: the JSON never crosses. */
  void (*origin_of)(const char* text, size_t len, RolltuiPutFn put, void* ctx);

  void* (*clone)(const void* value);
  void (*destroy)(void* value);
  int (*equal)(const void* a, const void* b);

  /* The ops for this domain's REPORT type: a BORROW of a library static, set by the domain's `_init`. A field, not a parameter at every
   * `_new` / `_shipped`, because no caller pairs a domain with any table but its own. */
  const RolltuiPresetReportFns* report;

  /* ---- THE DOMAIN'S OWN CONFIGURATION -------------------------------------------------
     * What each `*_preset_domain_init` was handed. It lives in the descriptor, not in file statics, so two descriptors of the same kind
     * do not share one configuration and a borrowed table cannot outlive the session that owned it. Only the rows for a descriptor's
     * own kind are read. */
  const RolltuiThemeVocab* theme_vocab;          /* theme */
  RolltuiThemePresetValidFn theme_mode_valid;    /* theme */
  RolltuiThemePresetValidFn theme_depth_valid;   /* theme */
  const RolltuiLayoutHooks* layout_hooks;        /* layout */
  const RolltuiLayoutAction* layout_actions;     /* layout — BORROWED from a context's cache */
  size_t layout_actions_n;                       /* layout */
  RolltuiScopeFn bindings_is_library_scope;      /* bindings */
  void* bindings_scope_ctx;                      /* bindings */
  RolltuiReasonFn bindings_reason;               /* bindings */
  void* bindings_reason_ctx;                     /* bindings */

  RolltuiPresetShippedCache* cache;

  /* ---- THE PERSON'S SETTINGS A VALUE CARRIES BESIDE ITS CONTENT ------------------------------------------------
     * A theme's colours are the theme's; whether they are shown light or dark, and at what colour depth, are the PERSON'S (a theme
     * value carries them only so one working file keeps the lot). The store asks the domain to keep the two apart; each hook is NULL
     * for a domain with no such settings:
     *   adopt_settings      `value`'s settings become `from`'s, or the defaults ("auto") when `from` is NULL. A preset never brings
     *                       settings of its own, and choosing one keeps the person's;
     *   settings_to_json    the settings as `,\n  "key": value` text (no braces) that a working file which only FOLLOWS its preset
     *                       writes beside the pointer, so the choice is remembered without freezing the preset's colours;
     *   settings_from_json  reads them back from that text.
     * `equal` then compares CONTENT only: choosing light is not an edit, an unedited working copy follows its preset, and what a
     * release improves in the preset reaches everyone who chose it. */
  void (*adopt_settings)(void* value, const void* from);
  void (*settings_to_json)(const void* value, RolltuiPutFn put, void* ctx);
  void (*settings_from_json)(const struct RolltuiPresetDomain* d, void* value, const char* text, size_t len);
} RolltuiPresetDomain;

/* Releases the parsed cache; it rebuilds on next use, so shutdown is callable at any moment. */

/* ---- the store ------------------------------------------------------------------------------ */

/* OWNED, LONG-LIVED: one per `rolltui::PresetStore<D>`, which frees it. Every method takes the store's own lock and hands back copies,
 * so a host may edit from one thread and render from another. */
typedef struct RolltuiPresetStore RolltuiPresetStore;

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
 * C++ destructor does that. */
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

/* ---- settings: the keys a person changes, over the three working copies ---------------------
 * Which working copy a key lives in is the library's business: a host holds one `RolltuiSettings` over its three stores and spells
 * keys, and the routing, the precedence and the sentence a bad value is refused with are all behind the handle. */
typedef struct RolltuiSettings RolltuiSettings;

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
  RolltuiStrList notes; /* what the load kept, rewrote or ignored — notes are not problems */
} RolltuiSettingsReport;

/* ---- forward declarations the C++ members just below call ----------------------------------*/
void rolltui_preset_list_release(RolltuiPresetList* l);

#ifdef __cplusplus
/* The one method that cannot be inline in the struct: it calls a function declared after it.
 * Same placement, and same reason, as `RolltuiStr::~RolltuiStr` in `rolltui_str.h`. */
inline RolltuiPresetList::~RolltuiPresetList() { rolltui_preset_list_release(this); }

#endif

/* ========================================================================================
 * swap — the double buffer, the newest entry point
 * ======================================================================================== */

typedef struct RolltuiSwap RolltuiSwap;

/* ========================================================================================
 * terminal — the one fd
 * ======================================================================================== */

/* ---- options, defined once and compiled by both languages ------------------------------ */
/* The attribute bits are `unsigned char`, not `bool`, for `rolltui_style.h`'s reason: one type in both languages, nothing to assume
 * about `_Bool` versus `bool`. */
#define ROLLTUI_DOUBLE_CLICK_MS 400 /* two presses of one button on one cell this close together are a double-click */

typedef struct RolltuiTerminalOptions {
  unsigned char alt_screen ROLLTUI_DEFAULT(1);
  unsigned char mouse ROLLTUI_DEFAULT(1);        /* SGR 1006 + button + drag reporting */
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
} RolltuiTerminalOptions;

typedef struct RolltuiTerminal RolltuiTerminal;

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
  RolltuiStyleColor background;
  unsigned char ambiguous_wide; /* 1: an ambiguous-width glyph takes two cells */
  unsigned char ambiguous_source;
  unsigned char keyboard;       /* ROLLTUI_PROTOCOL_*: how keys arrive */
  unsigned char responsive;     /* the terminal answered a question at all; 0 for a pipe or a silent one */
  unsigned char remembered;     /* this run used remembered answers rather than asking */
  char name[64];                /* how the terminal introduced itself, for humans: "Apple_Terminal 455" */
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
} RolltuiTermEvent;

/* Called once per event, in order. */
typedef void (*RolltuiTermEventFn)(void* ctx, const RolltuiTermEvent* e);

/* ========================================================================================
 * theme_analysis — the studio and the theme editor check themes; roll never does
 * ======================================================================================== */

/* ---- the three colour spaces, defined ONCE and compiled by both languages ------------- */
/* `rolltui::Lin`, `rolltui::OkLab` and `rolltui::OkLch` ARE these structs: linear sRGB, OKLab and OKLCH, each three doubles defaulting
 * to zero, with no methods. */
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

/* ---- badges: a fixed, closed set of 13 names — this module's OWN vocabulary, never a
 * Role's, so nothing below needs a vocab table to print or check one. --------------------- */
typedef struct RolltuiBadges {
  unsigned char dark ROLLTUI_DEFAULT(0), light ROLLTUI_DEFAULT(0), high_contrast ROLLTUI_DEFAULT(0),
      readable ROLLTUI_DEFAULT(0), cvd_safe ROLLTUI_DEFAULT(0);
  unsigned char protan_safe ROLLTUI_DEFAULT(0), deutan_safe ROLLTUI_DEFAULT(0), tritan_safe ROLLTUI_DEFAULT(0);
  unsigned char mono ROLLTUI_DEFAULT(0), safe_16 ROLLTUI_DEFAULT(0), safe_256 ROLLTUI_DEFAULT(0),
      transparent ROLLTUI_DEFAULT(0), attribute_redundant ROLLTUI_DEFAULT(0);
} RolltuiBadges;

/* A growing array of strings: this file's one shape for "a list of short diagnostic messages" (a report's failed badge claims).
 * `rolltui::Badges` is `using Badges = RolltuiBadges;`. */
typedef struct RolltuiStrArray {
  RolltuiStr* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0), cap ROLLTUI_DEFAULT(0);
} RolltuiStrArray;

/* Frees every element and the array; zeroes. Safe on a zeroed array and on repeated calls. */

/* ---- thresholds. `rolltui::kReadableRatio` etc. are `= ROLLTUI_*` aliases of these, so the
 * number is written down once. ------------------------------------------------------------ */

#define ROLLTUI_READABLE_RATIO 4.5

#define ROLLTUI_HIGH_CONTRAST_RATIO 7.0

#define ROLLTUI_DISTINCT_DELTA_E 0.08

/* The must-differ pairs' COUNT. The pairs are a LIBRARY RULE, closed on purpose (the decision and each pair's justification are at the
 * table, `kMustDiffer` in rolltui_theme_analysis.c). Only the count is a caller's business: it sizes the `out_pairs` array
 * `rolltui_theme_analyse` fills, and the pairs come back IN it (`RolltuiPairCheck.a` / `.b`). */
#define ROLLTUI_MUST_DIFFER_COUNT 11

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

/* ---- auto-fix proposals ------------------------------------------------------------------ */
typedef struct RolltuiFix {
  unsigned char role ROLLTUI_DEFAULT(0);
  RolltuiStyle before, after;
  RolltuiStr what;   /* OWNED — e.g. "md_link fg: contrast 3.1 -> 4.6" */
  double before_value ROLLTUI_DEFAULT(0), after_value ROLLTUI_DEFAULT(0);
} RolltuiFix;

/* A growing array of `RolltuiFix` (GROWING AMORTISED); each element owns its own `what`. */

typedef struct RolltuiFixArray {
  RolltuiFix* v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0), cap ROLLTUI_DEFAULT(0);
} RolltuiFixArray;

/* Releases every element's `what`, then the array; zeroes. Safe on a zeroed array and on
 * repeated calls. */

/* ========================================================================================
 * theme_gen — the theme generator is the editor's
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

/* ========================================================================================
 * undo — the editors' undo stack
 * ======================================================================================== */

/* Releases one snapshot the stack no longer holds: T's own destructor. Fixed for the life of a stack (one instance is always over one
 * T), so it is supplied once at `rolltui_undo_new`. */
typedef void (*RolltuiUndoFreeFn)(void* snapshot);

typedef struct RolltuiUndoStack RolltuiUndoStack;

/* ========================================================================================
 * widget_kinds — the built-in kinds and their registration
 * ======================================================================================== */

/* THE ROLE BYTES these kinds draw with, handed over ONCE at registration: this file names no role. Role ordinals are process-wide
 * constants, so a value copied in at construction never goes stale. */
typedef struct RolltuiBuiltinRoles {
  unsigned char text, text_muted, error, scroll_marker, label, value;
  unsigned char input_text, input_selection, input_placeholder;
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

/* ---- the engine ----------------------------------------------------------------------- */

/* ========================================================================================
 * PART 2 — THE HOST AUTHOR: load a thing, bind your own functions, run, release
 * Everything an app calls, in the order an app calls it. The order is measured: roll, paint, the explorer and the pure-C consumer
 * barely differ in WHICH calls they make, and those fall into load, bind, stack and compose, draw, run, release. If you are writing
 * an app, this part and Part 1 are the whole API.
 * ======================================================================================== */

/* ========================================================================================
 * THE SESSION — make one of these FIRST; everything a host does hangs off it
 * ======================================================================================== */

/* A ROLLTUI SESSION: the registries and caches an app configures, and nothing else (as process-wide statics they would make two
 * apps in one process share one widget-kind registry).
 *
 * THE CONTRACT
 *   1. One thread at a time: the library locks nothing inside a context. The exception is `RolltuiPresetStore`, which carries its own
 *      mutex so a host may edit from one thread and render from another.
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
 * LOAD — a screen, a theme, a bindings table and an app profile are FILES
 * A host reads them; it does not build them in code. What a file may name is Part 1's tables.
 * ======================================================================================== */

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

/* ---- theme ---------------------------------------------------------------------------------*/

/* COLORTERM=truecolor|24bit -> TrueColor; TERM containing "256color" -> Ansi256; TERM=dumb or empty -> Mono; else Ansi16. `force`
 * (ROLL_COLOR_DEPTH) wins when set and valid. Any argument may be NULL. */
unsigned char rolltui_detect_color_depth(const char* colorterm, const char* term, const char* force);

/* The mode a background implies: relative luminance (sRGB linearised, Rec. 709 weights)
 * above 0.5 is light, anything else — including a colour that is not rgb — is dark. */
unsigned char rolltui_mode_for_background(RolltuiStyleColor bg);

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

/* ---- source languages ------------------------------------------------------------------------
 * What colours a source file in a preview is a set of LANGUAGES, each a JSON file: the shipped ones, and any a person keeps in
 * `<config>/rolltui/syntax/` (one named like a shipped language replaces it). A file with a mistake is skipped, and nothing says so
 * on screen; this is how its author finds out. It loads the shipped set, the person's folder, and every file in `paths` (a language
 * being written; `path_count` may be 0), and writes into `out` what it found: how many shipped, which of the person's loaded (and
 * which replaced a shipped one), and for each file that did not, its name and why. Returns the number that did not load, so 0 is a
 * clean bill and a script can gate on it. */
size_t rolltui_syntax_check(const char* const* paths, size_t path_count, RolltuiStr* out);

/* ---- layout_tree ---------------------------------------------------------------------------*/

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
 *   3. the known folder   <ROLL_CONFIG_DIR|XDG_CONFIG_HOME/roll|$HOME/.config/roll>/rolltui/<app>/<kind>.json
 * `out` receives the winning contents. `tried` (may be NULL) receives one line per candidate, hit or miss, so a host that finds nothing can
 * say what it looked for. Returns 0 when every rung missed. */
int rolltui_app_file(const char* argv0, const char* app, const char* kind,
                     const RolltuiEmbeddedFile* embedded, size_t embedded_n,
                     RolltuiStr* out, RolltuiStr* tried);

/* Parses TEXT into a layout. A JSON syntax error becomes `report->error` (NULL returned). OWNED: the caller frees the result with
 * `rolltui_layout_free`. */
RolltuiLayout* rolltui_load_layout_text(const char* text, size_t len,
                                        const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                                        const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report);

/* The library's own three stack actions, expanded from the same closed list as the per-widget tables. BORROWS static storage; a host
 * with its own words still passes its own struct. */
const RolltuiStackActions* rolltui_stack_default_actions(void);

/* ---- embedded ------------------------------------------------------------------------------*/

/* The text of one shipped file by name, or NULL. A BORROW, valid for the process. */
const char* rolltui_embedded_text(const RolltuiEmbeddedFile* table, size_t count, const char* name,
                                  size_t name_len);

/* ---- presets -------------------------------------------------------------------------------*/

/* Reads a whole file; 0 when it cannot be opened. `put`/`ctx` because the library streams into its internal `Buf`; a consumer that wants
 * the bytes passes `rolltui_str_put` and a `RolltuiStr*`. */
int rolltui_preset_read_file(const char* path, size_t path_len, RolltuiPutFn put, void* ctx);

/* Writes to a sibling temp file, then renames: a reader sees the old complete file or the new complete file, never a mix. On failure, 0,
 * and the reason REPLACES `*err` (which may be NULL). */
int rolltui_preset_write_file_atomic(const char* path, size_t path_len, const char* bytes, size_t len,
                                     RolltuiStr* err);

/* The shipped presets, parsed once per domain into a report the domain makes for itself. A shipped preset that does not load cleanly is
 * a programming error: it says so and aborts, and so does a domain with no preset named "default". Returns a BORROW valid until the domain
 * is released; NULL for a name that is not shipped. */
const void* rolltui_preset_shipped(RolltuiPresetDomain* d, const char* name, size_t len);

int rolltui_preset_is_shipped(RolltuiPresetDomain* d, const char* name, size_t len);

/* The shipped preset's FILE TEXT, verbatim: a BORROW of the embedded bytes, valid for the process's life; NULL (and `*out_len` 0) when
 * `name` is not shipped. A lookup by NAME, since `shipped_count` / `shipped_at` give only indexes. */
const char* rolltui_preset_shipped_text(RolltuiPresetDomain* d, const char* name, size_t len, size_t* out_len);

/* The shipped names, "default" FIRST and the rest in table order — the order a chooser
 * offers them in. REPLACES `*out`. */
void rolltui_preset_shipped_names(RolltuiPresetDomain* d, RolltuiStrList* out);

RolltuiPresetStore* rolltui_preset_store_new(RolltuiPresetDomain* d, const char* dir, size_t dir_len,
                                             int may_write_shipped, const char* shipped_dir, size_t shipped_dir_len);

void rolltui_preset_store_free(RolltuiPresetStore* s);

/* Startup: the autosaved working copy when present and loadable, else "default". `report` says what happened to the working file; the
 * origin preset it names is re-read into a report the domain makes for itself, not a second one the caller must supply. */
void rolltui_preset_store_start(RolltuiPresetStore* s, void* report);

/* A CLONE the caller owns and frees with `rolltui_preset_store_value_free`. */
void* rolltui_preset_store_working(const RolltuiPresetStore* s);

/* Frees a value `_working` or `_get` handed back, by the store's own domain's `destroy`, so a caller holding the store alone can release
 * what it gave (a handle is created and released in a pair, rule 1). NULL is a no-op. */
void rolltui_preset_store_value_free(const RolltuiPresetStore* s, void* v);

/* BORROWS of the store's own bytes, valid until it next changes. */
const char* rolltui_preset_store_origin(const RolltuiPresetStore* s, size_t* len);

const char* rolltui_preset_store_last_error(const RolltuiPresetStore* s, size_t* len);

/* "(modified)" is BY COMPARISON, run when the store CHANGES and never when this is read: a read is one flag under the lock (running the
 * domain's deep `equal` on every call would put it on the draw path, twice a frame through `label`). */
int rolltui_preset_store_modified(const RolltuiPresetStore* s);

/* "<origin>", or "<origin> (modified)" once the working copy differs from what it was loaded from. THE LIBRARY OWNS THE WORD
 * "(modified)". REPLACES `*out`, reusing its buffer (it must not APPEND: a caller holding a buffer for its frame could not call it twice). */
void rolltui_preset_store_label(const RolltuiPresetStore* s, RolltuiStr* out);

unsigned long long rolltui_preset_store_version(const RolltuiPresetStore* s);

/* An in-place edit under the lock. */
void rolltui_preset_store_edit(RolltuiPresetStore* s, void (*fn)(void* value, void* ctx), void* ctx, int persist);

void rolltui_preset_list_release(RolltuiPresetList* l);

/* REPLACES `*out` (its capacity, and each entry's string buffers, are reused). */
void rolltui_preset_store_list(const RolltuiPresetStore* s, RolltuiPresetList* out);

/* A preset by name or path, as a value the caller OWNS and frees with
 * `rolltui_preset_store_value_free`; NULL with the report saying why. */
void* rolltui_preset_store_get(const RolltuiPresetStore* s, const char* name, size_t len, void* report);

/* …and the same, into the working copy. 0 when it could not be read. */
int rolltui_preset_store_load(RolltuiPresetStore* s, const char* name, size_t len, void* report, int persist);

/* The SENTENCE for an outcome: a table beside the constants it is indexed by. BORROWS a static literal; `*len` may be NULL; an
 * out-of-range code reads back as "". WRITE_FAILED's own reason is still the caller's, through `err` below. */
const char* rolltui_preset_save_result_text(int result, size_t* len);

/* Save-as. REPLACES `*err` (may be NULL) with the outcome's SENTENCE for every result but SAVED (WRITE_FAILED's own reason, and
 * `rolltui_preset_save_result_text`'s fixed sentence for the other three), so a caller reads one string for any refusal. */
/* Always autosaves the working copy afterwards, unlike `_load` / `_set_working` / `_edit`: a save-as is an explicit write and the working
 * copy records its new origin. There is no `persist` parameter: no caller wants 0. */
int rolltui_preset_store_save_as(RolltuiPresetStore* s, const char* name, size_t len, int overwrite, RolltuiStr* err);

/* ADD a preset from elsewhere (a theme someone sent you, a layout from another directory). ADDITIVE: nothing is closed and nothing is
 * replaced. The file is COPIED in rather than referenced, because a dangling reference is a preset that stops existing for a reason
 * nobody can see. Named by `as`, or by the file's own stem when `as` is empty. Returns the codes `save_as` does: `EXISTS_ASK` when the
 * name is taken (refused, never overwritten), `REFUSED_SHIPPED` for a shipped name, `BAD_NAME` for an unusable name OR a file that does
 * not parse as this domain, `WRITE_FAILED` when it could not be read or written. `err` carries the sentence either way. */
int rolltui_preset_store_add(RolltuiPresetStore* s, const char* path, size_t path_len, const char* as,
                             size_t as_len, RolltuiStr* err);

/* The two paths. Both REPLACE `*out` (rule 3(b)), like `rolltui_preset_store_label`. */
void rolltui_preset_store_working_path(const RolltuiPresetStore* s, RolltuiStr* out);

void rolltui_preset_store_preset_path(const RolltuiPresetStore* s, const char* name, size_t len, RolltuiStr* out);

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

/* ---- the `theme` widget kind's one call -----------------------------------------------------
 * An app gets a theme editor by NAMING `theme` in a layout and binding a key to whatever holds it; this is the only code it writes.
 * The theme is the APP's: the style table a frame is drawn with is passed into `rolltui_windows_draw` by the host, so no widget can
 * reach it. What the app hands over is the Theme preset store it already keeps: the editor loads its working copy, lists its presets
 * in the Load choice and writes every commit back, so an app that watches `rolltui_preset_store_version` picks a theme edit up the
 * way it picks up a loaded theme. An app with no store opens one.
 * `content` is the layout's own string for the window ("theme", or "theme:<anything>"); NULL or 0 means "theme". The widget is
 * CREATED if this screen has none yet, so a host may wire the store before its first draw. `persist` non-zero autosaves each commit
 * (what a session wants and a golden frame does not). The store is BORROWED and must outlive `w`. */
void rolltui_windows_set_theme_store(RolltuiWindows* w, const char* content, size_t len, RolltuiPresetStore* store,
                                     int persist);

/* ---- the `keys` widget kind's one call ------------------------------------------------------
 * An app gets a keys editor by NAMING `keys` in a layout and binding a key to whatever holds it. What it edits needs no call: the
 * editor's baseline is the table the stack already routes keys through, and a screen's own `app.*` actions are in the tree because
 * the screen declared them. What an app must say is WHERE A COMMIT GOES: the Bindings preset store it already keeps. The editor lists
 * its presets in the Load choice and writes every commit back, so an app that watches `rolltui_preset_store_version` picks a
 * rebinding up the way it picks up a loaded preset. `content`, `persist` and the store's lifetime are as for
 * `rolltui_windows_set_theme_store`. */
void rolltui_windows_set_bindings_store(RolltuiWindows* w, const char* content, size_t len,
                                        RolltuiPresetStore* store, int persist);

/* Whether the editor's "Load keys" / "Save keys as" / "Write a SHIPPED preset" appear at all (on by default). An app with exactly one
 * binding table turns this off and keeps edit, undo/redo and "Reset to the loaded preset". `content` as above. */
void rolltui_windows_set_keys_editor_show_presets(RolltuiWindows* w, const char* content, size_t len, int show);

void rolltui_theme_preset_value_release(RolltuiThemePresetValue* v); /* frees `colours`; zeroes */

void rolltui_layout_preset_report_release(RolltuiLayoutPresetReport* r); /* frees everything; zeroes */

void rolltui_bindings_preset_report_release(RolltuiBindingsPresetReport* r); /* frees everything; zeroes */

/* ---- IS THIS REPORT CLEAN, AND WHAT DOES IT SAY — per domain -------------------------------
 * `rolltui_preset_report_summary` above is the generic COMPOSER: it joins the pieces in order but does not know which pieces each
 * domain has. `_clean` is a JUDGEMENT about the library's own data ("does a missing role make a theme preset unclean?"), so it is the
 * library's: it returns 1 when the report has nothing to report. `_summary` APPENDS the sentence the composer would, with that
 * domain's fields already wired. */
int rolltui_theme_preset_report_clean(const RolltuiThemePresetReport* r);

void rolltui_theme_preset_report_summary(const RolltuiThemePresetReport* r, RolltuiStr* out);

int rolltui_layout_preset_report_clean(const RolltuiLayoutPresetReport* r);

void rolltui_layout_preset_report_summary(const RolltuiLayoutPresetReport* r, RolltuiStr* out);

int rolltui_bindings_preset_report_clean(const RolltuiBindingsPresetReport* r);

void rolltui_bindings_preset_report_summary(const RolltuiBindingsPresetReport* r, RolltuiStr* out);

/* One per library domain, named and never spelled as an id (no call site picks between the three at runtime). A domain's name is its
 * `kind`: read `dom->kind`. */
RolltuiPresetDomain* rolltui_preset_domain_theme(RolltuiContext* c);

RolltuiPresetDomain* rolltui_preset_domain_layout(RolltuiContext* c);

RolltuiPresetDomain* rolltui_preset_domain_bindings(RolltuiContext* c);

/* A setting's value in a store's WORKING COPY, APPENDED to `out` (empty when this key is not this domain's). The identity key (the
 * domain's `kind`: "theme" / "layout" / "bindings") answers with the origin; a Theme store also answers "theme_mode" and "color_depth".
 * It takes no domain tag beside the store: `kind` IS the name. */
void rolltui_preset_working_value(const RolltuiPresetStore* s, const char* key, size_t key_len, RolltuiStr* out);

/* ---- the settings handle --------------------------------------------------------------------
 * The three stores are BORROWED and must outlive the handle. */
RolltuiSettings* rolltui_settings_new(RolltuiPresetStore* theme, RolltuiPresetStore* layout,
                                      RolltuiPresetStore* bindings);

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
 * BIND — your own functions, sources and kinds
 * Everything the screen named that only your app can supply: the rows behind `rows:`, the
 * document behind a transcript, what a submit does, and any widget kind of your own.
 * ======================================================================================== */

/* ---- str -----------------------------------------------------------------------------------*/

/* Replaces the contents. `s` may be NULL only when `len` is 0. Keeps the buffer when it
 * already fits, which is what makes a re-assigned name free after the first. */
void rolltui_str_set(RolltuiStr* s, const char* text, size_t len);

/* ---- bindings ------------------------------------------------------------------------------*/

/* "ctrl+shift+left", "alt+enter", "f1", "escape", "?", "space": any modifier order, case-insensitive. 1 on success. A chord never carries
 * an Unknown key's raw bytes. */
int rolltui_chord_parse(const char* text, size_t len, RolltuiChord* out);

void rolltui_bindings_free(RolltuiBindings* b);

RolltuiBindings* rolltui_bindings_clone(const RolltuiBindings* b);

/* A BORROW, valid until the table next changes. */
int rolltui_bindings_has(const RolltuiBindings* b, const char* action, size_t len);

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

/* ---- document ------------------------------------------------------------------------------*/

void rolltui_doc_entry_release(RolltuiDocEntry* e);

void rolltui_doc_entry_copy(RolltuiDocEntry* to, const RolltuiDocEntry* from);

/* Appends an EMPTY entry and returns it — the C's `emplace_back`. */
RolltuiDocEntry* rolltui_document_add(RolltuiDocument* d);

void rolltui_document_clear(RolltuiDocument* d);   /* releases every entry, keeps the array */

void rolltui_document_release(RolltuiDocument* d); /* …and the array */

void rolltui_document_copy(RolltuiDocument* to, const RolltuiDocument* from);

/* ---- input ---------------------------------------------------------------------------------*/

/* The defaults, for a C caller — `= {0}` would give a zero tab width and no prompt role,
 * which is the language answering a question nobody asked. */
void rolltui_input_options_release(RolltuiInputOptions* o);

void rolltui_input_options_copy(RolltuiInputOptions* to, const RolltuiInputOptions* from);

int rolltui_input_options_equal(const RolltuiInputOptions* a, const RolltuiInputOptions* b);

void rolltui_input_set_copy(RolltuiInput* in, RolltuiCopyFn fn, void* ctx);

/* ---- content -------------------------------------------------------------------------------- */
/* A BORROW, valid until the text next changes. */
const char* rolltui_input_text(const RolltuiInput* in, size_t* len);

void rolltui_input_clear(RolltuiInput* in);
/* A host PREFILLS: a path line showing the folder the cursor is in, a name to correct. The text is copied, the caret goes to its end, the
 * selection is dropped. `select_all` is an address bar's rule on reaching it: typing replaces the whole, a first arrow key places the caret. */
void rolltui_input_set_text(RolltuiInput* in, const char* text, size_t len);
void rolltui_input_select_all(RolltuiInput* in);

/* The LIBRARY'S OWN thirty, so a consumer need not spell them to call `handle`. BORROWS static storage; a host with different words
 * still passes its own struct. */
const RolltuiInputActions* rolltui_input_default_actions(void);

/* ---- layout and drawing -------------------------------------------------------------------------- */
void rolltui_input_set_options(RolltuiInput* in, const RolltuiInputOptions* o);

const RolltuiInputOptions* rolltui_input_options(const RolltuiInput* in);

/* ---- menu_tree -----------------------------------------------------------------------------*/

/* Sets an item's kind, id and label in one call, releasing whatever they held: the C form of the builders above. `shortcut` may be NULL
 * with `shortcut_len` 0. */
void rolltui_menu_item_set(RolltuiMenuItem* it, unsigned char kind, const char* id, size_t id_len, const char* label,
                           size_t label_len, const char* shortcut, size_t shortcut_len);

void rolltui_menu_item_init(RolltuiMenuItem* it);

/* Frees everything below and inside `it` and leaves it clean: the pair to `_init`, and to the parser that FILLS a caller-owned item.
 * Without it a C consumer can build one and cannot free it. */
void rolltui_menu_item_release(RolltuiMenuItem* it);

void rolltui_menu_item_copy(RolltuiMenuItem* to, const RolltuiMenuItem* from);

int rolltui_menu_item_equal(const RolltuiMenuItem* a, const RolltuiMenuItem* b);

RolltuiMenuItem* rolltui_menu_list_add(RolltuiMenuItemList* l); /* an EMPTY child, appended */

void rolltui_menu_list_clear(RolltuiMenuItemList* l);           /* frees every child */

void rolltui_menu_list_release(RolltuiMenuItemList* l);         /* …and the array */

/* ---- effects -------------------------------------------------------------------------------*/

void rolltui_effect_map_free(RolltuiEffectMap* m);

int rolltui_effect_map_empty(const RolltuiEffectMap* m);

RolltuiEffectScratch* rolltui_effect_scratch_new(void);

void rolltui_effect_scratch_free(RolltuiEffectScratch* s);

/* ---- unicode -------------------------------------------------------------------------------*/

void rolltui_menu_event_release(RolltuiMenuEvent* e);

RolltuiMenuItem* rolltui_menu_root(RolltuiMenu* m);

/* Depth-first, any level; NULL when absent. A CHOICE's options are NOT searched: they are its VALUES, in their own id namespace, so an
 * option and a field may share an id and only the field is a thing to find. */
RolltuiMenuItem* rolltui_menu_find(RolltuiMenu* m, const char* id, size_t len);

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

void rolltui_menu_action_list_release(RolltuiMenuActionList* l);

/* REPLACES `*out` (reusing its buffers). Depth-first, the tree's own order. */
void rolltui_menu_item_actions(const RolltuiMenuItem* root, RolltuiMenuActionList* out);

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

/* THE SYNTAX HIGHLIGHTER every transcript this table owns renders code blocks through: a HOST fact, off until set. Pushed onto every
 * transcript that exists AND onto each one created afterwards, so the order a host calls this and `rolltui_windows_transcript` in cannot
 * matter. `ctx` is released through `free_ctx` when this is called again or when `w` is freed. */
void rolltui_windows_set_highlight(RolltuiWindows* w, RolltuiMdHighlightFn fn, void* ctx,
                                   void (*free_ctx)(void*));

/* THE EXTRA ROWS an `input:<source>` window must have whatever its text says (roll holds the prompt as tall as the modal placed over
 * it). Per-WINDOW sizing the widget keeps, not part of the edited text the input owns, so it is set here by source name. The widget is
 * created on demand, so a host may set the floor before any window has shown this input. */
void rolltui_windows_set_input_min_outer(RolltuiWindows* w, const char* source, size_t len, int rows);

/* documents: `doc` is a BORROW this table never frees; the host (or the table, for a sample built from markdown) keeps it alive. What
 * crosses is `&doc->entries`, the real `RolltuiDocument` the transcript kind reads directly. */
void rolltui_windows_bind_document(RolltuiWindows* w, const char* name, size_t len, const RolltuiDocument* doc);

/* …and the OWNED half: one entry of verbatim markdown this table keeps, for a host with no live `RolltuiDocument` to point at (sample
 * content in a preview, a fixed page of help, a C consumer showing one line). Binding the same name twice replaces the sample. The
 * `RolltuiDocument` is heap-held for the table's life, because the borrow above needs a stable address. */
void rolltui_windows_bind_sample_document(RolltuiWindows* w, const char* name, size_t len, const char* markdown,
                                          size_t markdown_len);

void rolltui_rows_reset(RolltuiRows* r);

void rolltui_rows_add(RolltuiRows* r, const char* label, size_t label_len, const char* value, size_t value_len);

void rolltui_rows_release(RolltuiRows* r);

void rolltui_windows_bind_rows(RolltuiWindows* w, const char* name, size_t len, RolltuiRowsFn fn, void* ctx,
                               void (*free_ctx)(void*));

void rolltui_windows_bind_submit(RolltuiWindows* w, const char* name, size_t len, RolltuiSubmitFn fn, void* ctx,
                                 void (*free_ctx)(void*), int on_submit);

void rolltui_windows_bind_note(RolltuiWindows* w, const char* name, size_t len, RolltuiNoteFn fn, void* ctx,
                               void (*free_ctx)(void*));

/* the preset directory: `file:`, `menu:`'s user rung and `menus/<name>.json` resolve against
 * this. A BORROW out, "" (never NULL) until `set_dir` is first called. */
void rolltui_context_set_dir(RolltuiContext* ctx, const char* dir, size_t len);

/* a menu file the HOST carries in its own binary: `menu:<name>`'s middle rung. The text is copied in; the borrow out is valid until that
 * name is bound again or `w` is freed. `_count` / `_name_at` enumerate every host menu. */
void rolltui_context_add_menu(RolltuiContext* ctx, const char* name, size_t len, const char* json, size_t json_len);

/* what a `help` window renders: an optional lead line, the scopes to list in order, an optional trailing note. `set_help` REPLACES
 * lead and note; the scope list is built separately (`clear_help_scopes`, then `add_help_scope` per entry), one call at a time. */
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

/* ---- widget_kinds --------------------------------------------------------------------------*/

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

/* Shared by the input plugin's own `handle` slot and by a host asking for the action back: handle `e` against the live bindings, and on
 * Submit either clear-and-push-history or keep the text (`rolltui_windows_on_submit` says which), then call whatever is bound to
 * `source`. Returns one of ROLLTUI_INPUT_IGNORED / HANDLED / SUBMIT / EOF. */
int rolltui_input_kind_process_event(RolltuiInput* ed, RolltuiWindows* w, const char* source, size_t source_len,
                                      const RolltuiEvent* e);

void rolltui_context_set_code_fold(RolltuiContext* ctx, const RolltuiCodeFold* c);

/* ========================================================================================
 * RUN — the terminal, the events, the frame
 * One fd, one event loop, one double buffer. The compose call walks the screen and hands each
 * resolved window back to you to draw.
 * ======================================================================================== */

/* ---- screen --------------------------------------------------------------------------------*/

/* ---- lifetime -------------------------------------------------------------------- */

/* ---- effects -------------------------------------------------------------------------------*/

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

/* ---- unicode -------------------------------------------------------------------------------*/

/* ---- sanitising ------------------------------------------------------------------------ */
/* Removes terminal control sequences from text that will be RENDERED. `out` must hold at least
 * `len` bytes — stripping only ever removes. Returns the bytes written. */
size_t rolltui_u_strip_escape_sequences(const char* s, size_t len, char* out);

/* Entry, then offset, then length — so of two positions at the same offset the one that
 * covers a grapheme is `last()`, and the range includes it. */
int rolltui_text_pos_less(const RolltuiTextPos* a, const RolltuiTextPos* b);

/* THE HOST'S CLIPBOARD, as a function pointer and a context. NULL turns it off. */
void rolltui_transcript_set_copy(RolltuiTranscript* t, RolltuiCopyFn fn, void* ctx);

/* The LIBRARY'S OWN eleven. BORROWS static storage. */
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

/* ---- folding ------------------------------------------------------------------------------------------ */
int rolltui_transcript_toggle_fold_nearest_top(RolltuiTranscript* t, const RolltuiDocument* doc);

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

/* ---- layout --------------------------------------------------------------------------------*/

/* The placement rule, in absolute coordinates (parent.x/y added), INTO a caller's rect rather than returned: `RolltuiRect` is not a
 * C++98 POD, so Clang refuses to promise an ABI for returning one from an `extern "C"` function (as for `RolltuiCell`). A rect PARAMETER
 * by value is fine. */
void rolltui_placement_resolve(const RolltuiPlacement* p, RolltuiRect parent, RolltuiRect* out);

RolltuiComposeScratch* rolltui_compose_scratch_new(void);

void rolltui_compose_scratch_free(RolltuiComposeScratch* s);

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

int rolltui_layout_report_clean(const RolltuiLayoutReport* r);

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
 * stack double-frees (measured by `c_consumer_test.c`). */
int rolltui_window_stack_push_popup(RolltuiWindowStack* s, const RolltuiLayout* layout, const char* id, size_t len);

int rolltui_window_stack_pop(RolltuiWindowStack* s); /* 0 when only the base remains */

size_t rolltui_window_stack_depth(const RolltuiWindowStack* s);

const RolltuiLayer* rolltui_window_stack_layer(const RolltuiWindowStack* s, size_t i);

int rolltui_window_stack_has_popup(const RolltuiWindowStack* s, const char* id, size_t len);

/* AN `app.<id>` ACTION NAMING A POPUP THE SCREEN DECLARES OPENS IT. Pass the action name (`"app.theme"`); when the part after `app.` names
 * a popup in `layout`, it is toggled and this returns 1; otherwise nothing happens and it returns 0, so a host tests its own actions
 * after calling this and an unknown name falls through. A screen gains a panel by adding one to its layout and one chord to its
 * bindings, with no host code. It TOGGLES: pressing the key that opened a panel closes it. A host that wants something else owns that
 * action itself. */
int rolltui_window_stack_action_popup(RolltuiWindowStack* s, const RolltuiLayout* layout,
                                      const char* action, size_t len);

#define ROLLTUI_SORT_NAME 0
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
typedef struct RolltuiPickerEvent {
  unsigned char kind ROLLTUI_DEFAULT(0);
  unsigned char inverse ROLLTUI_DEFAULT(0);
  RolltuiStr path;
} RolltuiPickerEvent;
void rolltui_picker_event_release(RolltuiPickerEvent* e);
int rolltui_windows_picker_event(RolltuiWindows* w, const char* content, size_t len, RolltuiPickerEvent* out);

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
} RolltuiPickerActions;

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

/* Routes one event. `window` is filled with the target id — a COPY, because the
 * ClosedPopup case names a layer this call has already freed. */
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

/* ---- presets -------------------------------------------------------------------------------*/

/* The frame as plain text, one row per line with trailing spaces trimmed. The golden-frame
 * harness is the caller; no escapes, no styles. */
void rolltui_frame_to_text(const RolltuiFrame* f, RolltuiStr* out);

/* ---- swap ----------------------------------------------------------------------------------*/

/* Both frames at `w` x `h`, filled with `fill`. Never returns NULL: an allocation failure
 * aborts through `rolltui_mem_alloc`, which is the library's stated answer. */
RolltuiSwap* rolltui_swap_new(int w, int h, RolltuiStyle fill);

void rolltui_swap_free(RolltuiSwap* s);

/* Resets the back frame to `w` x `h` and LENDS it for drawing. Valid until the next `begin` or `present`; the caller never frees it. A
 * size change is handled by `rolltui_render_diff` at present time. */
RolltuiFrame* rolltui_swap_begin(RolltuiSwap* s, int w, int h, RolltuiStyle fill);

/* Diffs the drawn frame against the previous one, APPENDS the bytes to `out`, and swaps.
 * After this the drawn frame is the baseline and the other is the next `begin`'s target.
 * Appends nothing when nothing changed and the cursor did not move. */
void rolltui_swap_present(RolltuiSwap* s, unsigned char depth, RolltuiStr* out);

/* "Repaint whole at the next present": THE HOST'S POLICY (a new layout, a new palette, an explicit repaint). A size change needs no call. */
void rolltui_swap_invalidate(RolltuiSwap* s);

/* The frame most recently presented, for a caller that needs to read it back — the golden
 * harness and `poll_timeout_ms` both do. Borrowed, valid until the next `present`. */
const RolltuiFrame* rolltui_swap_front(const RolltuiSwap* s);

/* ---- terminal ------------------------------------------------------------------------------*/

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

/* Writes every byte (loops on partial writes and EINTR). */
void rolltui_terminal_write(RolltuiTerminal* t, const char* bytes, size_t len);

/* ---- background colour (light/dark auto-detect) --------------------------------------------- */
/* Asks the terminal for its background colour (OSC 11) and waits up to timeout_ms, writing it to `*out`. Returns 0 on a pipe, on no
 * answer in time, or on an unparseable answer; the caller treats every 0 as "dark". Bytes that arrive and are not the reply (a user
 * already typing) are kept and delivered by the next `rolltui_terminal_poll`. Call once, before the event loop. */
int rolltui_terminal_query_background(RolltuiTerminal* t, int timeout_ms, RolltuiStyleColor* out);

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

/* ASKS AGAIN NOW, ignoring what was remembered, and remembers the new answers. Waits up to `timeout_ms`. 1 when the terminal answered, 0
 * when it did not (a pipe, a silent terminal); either way the facts are as good as they can be made. For a diagnostic
 * (`dirktui check-terminal`) and a host that knows the terminal changed under it. */
int rolltui_terminal_reprobe(RolltuiTerminal* t, int timeout_ms);

/* Where the remembered answers are read and written, or "" when this terminal was opened with
 * `no_cache`. A BORROW into the handle, valid until it is freed. */
const char* rolltui_terminal_cache_path(const RolltuiTerminal* t);

/* ---- the run loop ------------------------------------------------------------------------- */
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
} RolltuiRunApp;

/* Runs `app` on `in_fd`/`out_fd` until it stops. `opts` is `rolltui_terminal_new`'s, and `facts_events` is always on:
 * the loop is what listens. Returns ROLLTUI_RUN_STOPPED, ROLLTUI_RUN_NOT_A_TERMINAL or ROLLTUI_RUN_BAD_APP; in every
 * case the terminal is as it was found and nothing is left allocated. */
int rolltui_run(int in_fd, int out_fd, RolltuiTerminalOptions opts, const RolltuiRunApp* app);

/* WHAT THE CALLBACKS CALL, all no-ops on NULL. Leave the loop after this wake: the events of the batch that follow
 * are dropped, `settle` is still called once. */
void rolltui_run_stop(RolltuiRun* run);
/* The terminal the run entered, for `rolltui_terminal_facts` and the like. A BORROW valid for the run; the app never
 * frees it, and never calls `rolltui_terminal_poll` or `rolltui_terminal_present` on it — that is the loop's. */
RolltuiTerminal* rolltui_run_terminal(RolltuiRun* run);
/* Repaint the whole screen at the next frame: a keypress that asks for it, a change of palette. */
void rolltui_run_invalidate(RolltuiRun* run);
/* HAND THE TERMINAL TO A CHILD PROGRAM AND TAKE IT BACK (an editor, a pager): `suspend` leaves the alternate screen and restores the tty's
 * own modes; the child runs on the same descriptors; `resume` enters again AND repaints the whole screen, because what was on it is gone. */
void rolltui_run_suspend(RolltuiRun* run);
void rolltui_run_resume(RolltuiRun* run);

/* ========================================================================================
 * RELEASE — and the number that proves you did
 * There is no RAII in C: create and release in a pair, then `rolltui_shutdown()` and assert
 * `live_bytes == 0`. That is how a missed release is caught rather than hoped about.
 * ======================================================================================== */

/* ---- embedded ------------------------------------------------------------------------------*/

/* Releases everything the library retains: every registered process-wide releaser (most recently registered first), then the CALLING
 * thread's scratch via `rolltui_release_thread` (another thread's `_Thread_local` storage cannot be freed from here). Safe to never call
 * and safe to call twice. Nothing is invalidated for a host that carries on: the caches rebuild on next use. */
void rolltui_shutdown(void);

/* ---- mem -----------------------------------------------------------------------------------*/

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
/* What goes BETWEEN hints (two spaces by default; " › " makes a breadcrumb), and whether the bar keeps its TAIL when short of room (the
 * last hints drawn whole, the head replaced by an ellipsis) rather than dropping whichever hints do not fit. A breadcrumb keeps its tail:
 * the place the eye is at, and the pencil after it, must always be there to click. With the tail kept the LAST hint is the bar's own mark
 * rather than a part, and a single space joins it. */
void rolltui_hint_bar_set_separator(RolltuiHintBar* b, const char* sep, size_t len);
void rolltui_hint_bar_set_keep_tail(RolltuiHintBar* b, int on);
/* A hint whose action cannot be taken now is DISABLED: drawn in `muted`, never hit. Set by
 * action name, every frame if need be — it allocates nothing. */
void rolltui_hint_bar_enable(RolltuiHintBar* b, const char* action, size_t len, int on);
/* Draws from (x, y) within `width` cells and returns the cells used. */
int rolltui_hint_bar_draw(RolltuiHintBar* b, RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, int width,
                          RolltuiStyle chord_style, RolltuiStyle label_style, RolltuiStyle muted, int ambiguous_wide);
/* The action drawn under (x, y) on the last draw — a BORROW into the bar — or NULL. */
const char* rolltui_hint_bar_hit(const RolltuiHintBar* b, int x, int y, size_t* len);

/* ========================================================================================
 * PART 3 — THE WIDGET AUTHOR: what implementing a kind needs, and nothing else
 * A widget is nine slots on `RolltuiWidgetPlugin` (destroy, layout, draw, handle, desired_outer, scroll_extent, problem, note_at, and
 * the ctx they share), registered with `rolltui_windows_register_kind` (Part 2: REGISTERING is the host's act, IMPLEMENTING is yours).
 * What fills those slots is here: draw into a frame, measure and fit text, read a style, mark a span for the theme's effects, and
 * read your own rect off the resolved node you are handed.
 * DO draw only through these calls. An effect never invents a colour (it picks a ROLE the theme named), but that rule binds EFFECTS,
 * not you: a widget writes any `RolltuiStyle` into any cell (paint's canvas is a whole app built on that), and marks a span with a
 * STATE when it wants the theme to animate it.
 * ======================================================================================== */

/* ---- screen --------------------------------------------------------------------------------*/

/* `state` is rolltui::EffectState as an int; the C side stores it and never interprets it, which keeps the effects vocabulary in one
 * place. 0 means None and is not recorded, so "is anything marked" and "does anything move" stay the same question. */
void rolltui_frame_mark(RolltuiFrame* f, int x, int y, int cells, int state,
                        unsigned long long since_ms, double fraction);

size_t rolltui_frame_mark_count(const RolltuiFrame* f);

/* ---- cursor ------------------------------------------------------------------------ */
void rolltui_frame_set_cursor(RolltuiFrame* f, int x, int y, int visible);

/* ---- frame_ops -----------------------------------------------------------------------------*/

RolltuiDrawScratch* rolltui_draw_scratch_new(void);

void rolltui_draw_scratch_free(RolltuiDrawScratch* s); /* a no-op on NULL */

/* Writes `utf8` at (x, y), cluster by cluster, stopping at `max_cells`, at the frame's right
 * edge, or before a wide glyph that would be cut in half. Returns the cells used. */
int rolltui_frame_put_text(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const char* utf8, size_t len,
                           RolltuiStyle style, int max_cells, int ambiguous_wide, unsigned int link);

/* ---- a colour, shown ---------------------------------------------------------------------
 * ONE WAY A COLOUR VALUE IS PUT ON THE SCREEN, wherever one is (a theme editor's role, a menu's colour input, a painter's ink): its
 * spelling (`rolltui_color_to_string`'s "#rrggbb", palette index or "none") and beside it its SWATCH, two cells showing the colour as it
 * will be drawn (through the same renderer, at the terminal's depth), framed in the text's own foreground so it shows against any
 * ground. A `none` colour is the frame alone. */

/* The swatch: `frame`'s foreground is the outline and its background is where a `none` colour lands. Returns the cells used: 2, or 0
 * where they do not fit (`max_cells` < 2) or the terminal draws no colour. Where an ambiguous glyph is two cells the outline is a bracket
 * pair, one cell wherever it is drawn. */
int rolltui_frame_put_swatch(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, RolltuiStyleColor colour,
                             RolltuiStyle frame, int max_cells, int ambiguous_wide);

/* The spelling and, before it, the swatch and a space: `▏▕ #d8dce2`. `text_style` styles the
 * spelling and frames the swatch. Returns the cells used. */
int rolltui_frame_put_colour(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, RolltuiStyleColor colour,
                             RolltuiStyle text_style, int max_cells, int ambiguous_wide);

/* Fills `r` (clipped) with a repeated grapheme — a space when `glyph` is NULL or has no
 * width. */
void rolltui_frame_fill(RolltuiFrame* f, RolltuiDrawScratch* s, RolltuiRect r, RolltuiStyle style,
                        const char* glyph, size_t glyph_len);

/* ONE ROW OF NAME/VALUE FIELDS, each name in `name_style` and each value in `value_style`, separated by two spaces and stopping at
 * `max_cells`. Returns the cells used. It is what a status line is: a handful of facts, each with a name (one string in one style is a
 * wall of words). A row with an EMPTY LABEL draws its value alone (a title, a bracketed note); an empty VALUE draws the name alone (a
 * flag). It takes a `RolltuiRows` the caller owns and refills, so a warm frame allocates nothing: `rolltui_rows_reset` keeps the array
 * and every row's buffer. */
int rolltui_frame_put_fields(RolltuiFrame* f, RolltuiDrawScratch* s, int x, int y, const RolltuiRows* rows,
                             RolltuiStyle name_style, RolltuiStyle value_style, int max_cells,
                             int ambiguous_wide);

/* The cells `utf8` WOULD occupy, measured with the same cluster walk that draws it (`rolltui_frame_put_text` answers only after drawing,
 * and a caller sizing a column must know first). Same walk, same ambiguous-width rule, same result. */
int rolltui_frame_text_width(RolltuiDrawScratch* s, const char* utf8, size_t len, int ambiguous_wide);

/* ---- theme ---------------------------------------------------------------------------------*/

/* A BORROW into the caller's own table, valid exactly as long as `styles` is. NULL when
 * `styles` is NULL or `role` is out of range — a bad ordinal is a caller bug, not a crash. */
const RolltuiStyle* rolltui_theme_style(const RolltuiStyle* styles, size_t role_count, unsigned char role);

/* ---- unicode -------------------------------------------------------------------------------*/

RolltuiUnicodeScratch* rolltui_u_scratch_new(void);

void rolltui_u_scratch_free(RolltuiUnicodeScratch* s);

int rolltui_u_display_width(RolltuiUnicodeScratch* s, const char* utf8, size_t len, int ambiguous_wide);

/* How many BYTES of `utf8` fit in `max_cells` columns: the offset `rolltui_frame_put_text` computes to honour its own `max_cells`.
 * Returns that byte length and fills `*out_cells` (may be NULL) with the columns they occupy. Stops BEFORE a glyph that would be cut in
 * half, put_text's rule, so the two agree about where a cut falls. A widget that truncates needs it to place an ellipsis, and every
 * list, tree, table and column view truncates. `s` is the caller's scratch (rule 4). */
size_t rolltui_u_fit(RolltuiUnicodeScratch* s, const char* utf8, size_t len, int max_cells,
                     int ambiguous_wide, int* out_cells);

/* ---- layout --------------------------------------------------------------------------------*/

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

/* ---- diff ----------------------------------------------------------------------------------*/

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

/* ---- marker --------------------------------------------------------------------------------*/

size_t rolltui_scroll_marker_text(size_t below, int max_width, int ambiguous_wide, char* out, size_t cap);

/* ---- wrap ----------------------------------------------------------------------------------*/

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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_H */

