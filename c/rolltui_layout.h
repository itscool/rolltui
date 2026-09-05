#ifndef ROLLTUI_C_LAYOUT_H
#define ROLLTUI_C_LAYOUT_H
/*
 * rolltui/c/rolltui_layout.h — PLACEMENT, COMPOSITION, THE STACK AND THE LOADER (Phase 15 m5,
 * the loader and every English sentence added at Phase 17 m2).
 *
 * The algorithm half of the layout module: what a Dim resolves to, how a Row divides its
 * width, which borders join, which window has focus and where an event goes, and — since
 * Phase 17 m2 — how a layout FILE turns into that tree and back. Every rule is stated in
 * `rolltui/Layout.hpp` and asserted in `rolltui/tests/layout_test.cpp`; none of it is
 * repeated here. The DATA it walks is `rolltui_layout_tree.h`, which is C unconditionally: m5
 * built `-DROLLTUI_C` as a two-implementation rollback flag, and CMakeLists.txt's own note
 * records that flag as SPENT as of 2026-09-04 — `LayoutCpp.cpp` (this file's one-time C++
 * counterpart) is deleted along with the other fifteen `*Cpp.cpp` files, and this is now the
 * only implementation, not one side of a flag.
 *
 * ---- THE LIFETIME THIS FILE MAKES EXPLICIT ----------------------------------------------
 *
 * **`WindowStack` owns its layers.** In C++ that was `std::vector<Layer> layers_{Layer{}}`,
 * and the ownership was three separate accidents: the vector deep-copied a whole tree on
 * `set_base`, `push(Layer)` took one BY VALUE so a popup's tree was copied twice on the way
 * in, and a `Layer&` handed out by `base()` dangled the moment a popup pushed. Here the
 * stack is an opaque handle that owns an array of layers it MOVES into place, `push` takes
 * a layer and leaves the caller's empty, and `base()` is a call rather than a reference kept
 * across a mutation.
 *
 * ---- THE BOUNDARY'S RULES, all inherited from Phase 14 and none new ----------------------
 *
 *   1. **THE CALLER OWNS EVERY BUFFER**, including working memory: `rolltui_compose_layer`
 *      needs two screen-sized byte maps and takes a SCRATCH handle rather than keeping a
 *      `thread_local` of its own, which is CLAUDE.md's strategy 3 as amended on 2026-09-04.
 *   2. **NOTHING IS RETURNED BY VALUE** except plain scalars and the two POD geometry
 *      structs that are one definition in both languages.
 *   3. **NO `std::function` CROSSES.** A resolve EMITS THROUGH A SINK and a compose calls a
 *      slot renderer through a function pointer and a `void*` — the shape m4 fixed for the
 *      highlighting seam, and for the same reason: the caller decides where the nodes go.
 *   4. **THIS FILE NAMES NO ROLE AND NO ACTION.** The three roles a border needs and the
 *      three stack actions are HANDED IN as bytes and as strings, the m2 rule at
 *      `rolltui_diff.h` and the m3 rule at `rolltui_bindings.h`'s Enter rule. The C knows
 *      "the focused window's border uses this role" and none of the words.
 *
 * ---- WHAT THIS BOUNDARY DELIBERATELY DOES NOT KNOW ---------------------------------------
 *
 * **`rolltui::Layout`, `rolltui::Content` and `rolltui::ActionDecl`'s own shapes.** All three
 * keep their `std::string`/`std::vector` fields in `Layout.hpp` — the same exception
 * `rolltui_json.h`'s `Value` took, and for the same reason: `Widgets.cpp`, `paint.cpp`,
 * `studio.cpp` and `layout_editor.cpp` read `Content::source`, erase-remove and reassign
 * `Layout::actions` as a real `std::vector<ActionDecl>` at call sites this file does not
 * touch. What crosses below is the ALGORITHM (JSON in, JSON out, which rung, what rule) and
 * every SENTENCE a bad layout produces (moved from `Layout.cpp` at Phase 17 m2, once
 * `rolltui_json.h` gave this file a tree to walk that owed nothing to `json::Value`);
 * `RolltuiLoadedLayout` below is the transient, C-shaped carrier the shim unpacks into its
 * own `Layout` once per load and never retains.
 *
 * **Role names.** `background`'s vocabulary is `Style.hpp`'s (`rolltui_layout_tree.h`'s own
 * rule: "this file names no role"), so the loader and the dumper ask back through
 * `RolltuiLayoutHooks::role_from_name`/`role_name` rather than carrying a table of their own
 * — the same shape `RolltuiScopeFn` already uses for "which scopes are the library's".
 * Anchor and border names, by contrast, are THIS module's own vocabulary (Layout.hpp states
 * both), so they are declared and looked up right here, no callback needed.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_frame_ops.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_keys.h"
#include "rolltui/c/rolltui_layout_tree.h"
#include "rolltui/c/rolltui_screen.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
#include <string_view>

namespace rolltui {
// ONE spelling of a content's kind, whether it is the library's own (rung 1) or a host's
// registered one (rung 2, `Registered`) — Layout.hpp states the vocabulary; the enum lives
// here, beside the registry that resolves it, for the same reason `Border`/`Anchor` are
// declared beside `RolltuiLayoutNode` in `rolltui_layout_tree.h` rather than left to a
// C++-only header: `RolltuiContent` below needs a concrete type for its `kind` field. An
// opaque, already-complete enum with a fixed underlying type — nothing about this changes by
// moving; every existing `WidgetKind::Transcript` etc. still names the same value.
enum class WidgetKind : unsigned char { Transcript, Input, Menu, Rows, Text, File, Help, Registered };
}  // namespace rolltui
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- placement --------------------------------------------------------------------------- */

/* floor(fraction * extent + 1e-6) + cells. */
int rolltui_resolve_dim(RolltuiDim d, int extent);
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
/* The inner rect once the border is taken off (a None border takes nothing). */
void rolltui_inner_rect(RolltuiRect outer, unsigned char border, RolltuiRect* out);

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
/* "fill" | "fill 2" | a dim string. */
int rolltui_parse_split_size(const char* text, size_t len, RolltuiSplitSize* out);
/* The same, plus a bare integer as cells — a size as TYPED. */
int rolltui_parse_size_text(const char* text, size_t len, RolltuiSplitSize* out);
size_t rolltui_split_size_to_string(RolltuiSplitSize s, char* out, size_t cap);

/* ---- the split ----------------------------------------------------------------------------- */

/* Called once per node, in TREE ORDER (a container precedes its children). The caller
 * decides where they go — a vector, a filter, a single hit test. */
typedef void (*RolltuiResolvedSink)(void* ctx, const RolltuiResolvedNode* rn);

/* Lays out one tree inside `box` (a layer's resolved placement). Hidden nodes are omitted,
 * and a hidden ROOT emits nothing at all. */
void rolltui_resolve_tree(const RolltuiLayoutNode* root, RolltuiRect box, RolltuiRect screen, size_t layer,
                          RolltuiResolvedSink emit, void* ctx);

/* ---- drawing --------------------------------------------------------------------------------- */

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

/* Draws one border with NO joining — for a widget that boxes its own content. `title` may
 * be NULL when `title_n` is 0. */
void rolltui_draw_border(RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect outer, unsigned char border,
                         RolltuiStyle line, const char* title, size_t title_n, RolltuiStyle title_style,
                         int ambiguous_wide);

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

/* Draws the nodes of ONE layer, in the order given. `styles` is `kRoleCount` styles — the
 * theme's table, a BORROW for the call. */
void rolltui_compose_layer(RolltuiFrame* f, const RolltuiResolvedNode* nodes, size_t count,
                           const RolltuiStyle* styles, const RolltuiLayoutRoles* roles, RolltuiSlotFn render,
                           void* ctx, int ambiguous_wide, RolltuiComposeScratch* scratch);

/* ---- the widget-kind registry ------------------------------------------------------------------ */
/* Rung 1 is a CLOSED table this file holds; rung 2 is what a host registered, which is a
 * process-wide retainer released by `rolltui::shutdown()`. What a kind's source is CALLED
 * lives here too now (below, `rolltui_content_parse`/`_format`) — moved from the shim at
 * Phase 17 m2, since composing the sentence needs nothing `rolltui::Content`'s own
 * std::string shape supplies that this file cannot already answer itself. */

#define ROLLTUI_SOURCE_REQUIRED 0
#define ROLLTUI_SOURCE_OPTIONAL 1
#define ROLLTUI_SOURCE_FORBIDDEN 2

/* Which rung answered, which is the whole of what the C decides. */
#define ROLLTUI_KIND_UNKNOWN 0  /* neither rung */
#define ROLLTUI_KIND_LIBRARY 1  /* rung 1, and `*ordinal` is its WidgetKind */
#define ROLLTUI_KIND_HOST 2     /* rung 2 */

/* Resolves a kind NAME. `rule` and `source_is` (a BORROW valid until the registry changes)
 * are filled for the two answering rungs; any out-param may be NULL. */
int rolltui_widget_kind_resolve(const char* name, size_t len, unsigned char* ordinal, unsigned char* rule,
                                const char** source_is, size_t* source_is_len);
/* The library's closed table, in table order. */
size_t rolltui_widget_kind_library_count(void);
const char* rolltui_widget_kind_library_name(size_t i, size_t* len);
unsigned char rolltui_widget_kind_library_rule(size_t i);
const char* rolltui_widget_kind_library_source_is(size_t i, size_t* len);
/* …and rung 2, in registration order. */
size_t rolltui_widget_kind_host_count(void);
const char* rolltui_widget_kind_host_name(size_t i, size_t* len);

/* Why a registration was refused, so the shim can say it in words. 0 is accepted. */
#define ROLLTUI_REGISTER_OK 0
#define ROLLTUI_REGISTER_EMPTY 1
#define ROLLTUI_REGISTER_HAS_COLON 2
#define ROLLTUI_REGISTER_IS_LIBRARY 3    /* rung 1 is never shadowed */
#define ROLLTUI_REGISTER_RULE_DIFFERS 4  /* already registered, with another source rule */
int rolltui_widget_kind_register(const char* name, size_t len, unsigned char rule, const char* source_is,
                                 size_t source_is_len);
void rolltui_widget_kind_clear(void);

/* Phase 9's bare slot names and Phase 10's `custom:` contents → their m3 spelling. A BORROW
 * of a constant; NULL when `legacy` is not one of them. */
const char* rolltui_migrated_content(const char* legacy, size_t len, size_t* out_len);

/* ---- content: the value type, and parsing/formatting it, with the English (Phase 17 m2/m5).
 * `rolltui::Content` IS `RolltuiContent` below — the Phase 14 one-definition rule, same as
 * `Node`/`Layer` — so `Widgets.cpp`'s `Content content;` member and the ~20 sites reading
 * `.source` are reading a `RolltuiStr` now rather than a `std::string`; the field keeps every
 * operation those sites use (`.data()`, `.size()`, `.empty()`, `==`, assignment from a
 * `std::string`/`string_view`), so most survive unchanged, the same property that let
 * `rolltui_document.h`'s port leave ~82 call sites untouched. The DECISION and every SENTENCE
 * a bad content produces live entirely in the functions below; the shim (`Layout.cpp`) only
 * slices `text` at the offsets handed back. ------------------------------------------------- */

/* PLAIN DATA: `kind` (POD enum) plus two `RolltuiStr`s. Every member already has correct
 * value semantics on its own (`RolltuiStr`'s, `WidgetKind`'s as a scalar), so — exactly like
 * `RolltuiLayoutNode` one level up, which relies on the same thing for its three `RolltuiStr`s
 * and its `RolltuiNodeList` — this type declares NO constructor, destructor or assignment of
 * its own: the compiler-generated ones already do the right thing by construction, and
 * `operator==` needs only `= default` because `RolltuiStr::operator==` already exists. */
typedef struct RolltuiContent {
#ifdef __cplusplus
  rolltui::WidgetKind kind = rolltui::WidgetKind::Text;
#else
  unsigned char kind;
#endif
  RolltuiStr source;          /* the part after the first ':' — a bound name, a literal, a path */
  RolltuiStr registered_name; /* the host's kind name; empty for every library kind */
#ifdef __cplusplus
  bool operator==(const RolltuiContent&) const = default;
#endif
} RolltuiContent;

/* A C caller's pair, for the same reason every owned type here has one — `kind` becomes Text
 * and both strings empty either way. C++ needs neither (see above) but they exist so a pure
 * C caller has the same capability. */
void rolltui_content_init(RolltuiContent* c);
void rolltui_content_release(RolltuiContent* c);
void rolltui_content_copy(RolltuiContent* to, const RolltuiContent* from);
int rolltui_content_equal(const RolltuiContent* a, const RolltuiContent* b);

#define ROLLTUI_CONTENT_PROBLEM_NONE 0
#define ROLLTUI_CONTENT_PROBLEM_UNKNOWN_KIND 1
#define ROLLTUI_CONTENT_PROBLEM_MISSING_SOURCE 2
#define ROLLTUI_CONTENT_PROBLEM_FORBIDDEN_SOURCE 3

/* Parses "kind[:source]". 1 on success: `ordinal` is the library WidgetKind (rung 1) when
 * `*is_host` comes back 0, meaningless when it comes back 1 (rung 2 — the shim reads the
 * registered name back through `name`/`name_len` instead). `name`/`name_len` (the part
 * before the colon) and `source`/`source_len` (the part after, "" with a valid pointer when
 * there was none) are always filled and are BORROWS into `text` — never a copy, because a
 * caller that wants a `std::string` is about to make one anyway (Content's own shape). On
 * failure (0): `problem` says which of the three ways (never None), and `why` — cleared on
 * entry — gets the exact sentence `rolltui::parse_content` always produced. */
int rolltui_content_parse(const char* text, size_t len, unsigned char* ordinal, int* is_host, const char** name,
                         size_t* name_len, const char** source, size_t* source_len, unsigned char* problem,
                         RolltuiStr* why);
/* content_to_string's join rule: `kind_name`, then ":" + `source` exactly when `rule` says
 * the colon belongs (Required always; Optional only when `source` is non-empty). REPLACES
 * `*out`. */
void rolltui_content_format(const char* kind_name, size_t kind_name_len, const char* source, size_t source_len,
                            unsigned char rule, RolltuiStr* out);

/* ---- names: anchors and borders are THIS module's own vocabulary (Layout.hpp states both
 * closed lists), unlike Role — see the header comment. ------------------------------------- */

const char* rolltui_anchor_name(unsigned char a, size_t* len); /* "" when `a` is out of range */
int rolltui_anchor_from_name(const char* name, size_t len, unsigned char* out);
const char* rolltui_border_name(unsigned char b, size_t* len);
int rolltui_border_from_name(const char* name, size_t len, unsigned char* out);

/* ---- the loader: a layout file's JSON, in both directions, as C (Phase 17 m2) ------------
 *
 * `load_layout`, `layout_to_json[_value]`, the built-ins' per-file parse and every message a
 * bad layout produces move here from `Layout.cpp`, now that `rolltui_json.h` gives this file
 * a tree it can walk without owing `json::Value` anything. What does NOT move is stated at
 * the top of this header: `rolltui::Layout` (name + actions need real std::string/vector
 * semantics at call sites outside this port's scope) and Role's vocabulary (Style.hpp's).
 * `RolltuiLayoutHooks` is how this file reaches back for both without naming either. */

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

/* ---- the report: unknown keys / bad values are problems, migrations are notes (Layout.hpp's
 * `LayoutLoadReport::clean()`). Transparent, the same shape `rolltui_app_profile.h`'s own
 * report uses: `RolltuiStr` values in GROWING AMORTISED arrays. Zero-initialise before use. */
typedef struct RolltuiLayoutReport {
  RolltuiStr error; /* non-empty: the file was unusable */
  RolltuiStr* unknown_keys;
  size_t unknown_keys_n, unknown_keys_cap;
  RolltuiStr* bad_values;
  size_t bad_values_n, bad_values_cap;
  RolltuiStr* migrated; /* NOT part of "clean" — a note, never a problem */
  size_t migrated_n, migrated_cap;
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
  RolltuiActionList(const RolltuiActionList& o) { copy_from(o); }
  RolltuiActionList(RolltuiActionList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiActionList& operator=(const RolltuiActionList& o) {
    if (this != &o) copy_from(o);
    return *this;
  }
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
  void erase_name(std::string_view name);
  void clear();
  bool operator==(const RolltuiActionList& o) const;

 private:
  void copy_from(const RolltuiActionList& o);
#endif
} RolltuiActionList;

void rolltui_action_list_release(RolltuiActionList* l);
void rolltui_action_list_clear(RolltuiActionList* l);
void rolltui_action_list_copy(RolltuiActionList* to, const RolltuiActionList* from);
size_t rolltui_action_list_count(const RolltuiActionList* l);
RolltuiLayoutAction* rolltui_action_list_at(const RolltuiActionList* l, size_t i);
/* Appends an EMPTY action and returns it — the C's `emplace_back`. */
RolltuiLayoutAction* rolltui_action_list_add(RolltuiActionList* l);
void rolltui_action_list_remove(RolltuiActionList* l, size_t i); /* frees it, shifts the rest down */
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
  const RolltuiLayer* popup(std::string_view id) const {
    for (std::size_t i = 0; i < popups.size(); ++i)
      if (popups[i].id == id) return &popups[i];
    return nullptr;
  }
  bool operator==(const RolltuiLayout&) const = default;
#endif
} RolltuiLayout;

void rolltui_layout_init(RolltuiLayout* l);    /* zeroes; inits `base` */
void rolltui_layout_release(RolltuiLayout* l); /* frees name/actions/base/popups; zeroes */
void rolltui_layout_copy(RolltuiLayout* to, const RolltuiLayout* from);
int rolltui_layout_equal(const RolltuiLayout* a, const RolltuiLayout* b);
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

/* Reads exactly the "actions" object of an already-parsed tree, APPENDING every string-
 * valued entry. Never through `rolltui_load_layout`, which asks for this when a file
 * declares none — going through the full loader to compute its own fallback would recurse
 * into itself; this is the raw, independent read `rolltui::shipped_default_actions()` needs
 * (one definition site is still the "default" file; this is a direct read of one key of it). */
void rolltui_layout_read_actions_key(const RolltuiJsonValue* root, RolltuiLayoutAction** actions, size_t* actions_n,
                                    size_t* actions_cap);
/* Releases an array `rolltui_layout_read_actions_key` filled (or any array of this shape) —
 * so a caller need not reach past this header for `rolltui_alloc.h`'s raw `rolltui_mem_free`
 * just to hand one back. */
void rolltui_layout_actions_free(RolltuiLayoutAction* actions, size_t n);

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


/* Parses one layout file's ALREADY-PARSED JSON tree into `out` (an `out` the caller has run
 * `rolltui_loaded_layout_init` on — its old fields are not released first, matching
 * `rolltui::load_layout`'s "everything else loads with the problems reported" only at the
 * level a fresh handle already gives it). 1 when there is a usable "root" (`report` may
 * still carry problems); 0 only when the JSON is not an object or has none of it
 * (`report->error` says which — `out` is left as `rolltui_loaded_layout_init` set it).
 * `default_actions`/`_n` are `shipped_default_actions()`'s, handed in for the "no actions
 * key at all" fallback (see `rolltui_layout_read_actions_key` for why that is computed
 * independently rather than through this same function). `report` is NOT reset on entry —
 * matching `rolltui::load_layout`'s own convention, the caller starts one fresh per call. */
int rolltui_load_layout(const RolltuiJsonValue* root, RolltuiLoadedLayout* out,
                        const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                        const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report);
/* The same over TEXT: parses it first, and a JSON syntax error also becomes `report->error`
 * (0 returned) rather than reaching the loader at all. */
int rolltui_load_layout_text(const char* text, size_t len, RolltuiLoadedLayout* out,
                             const RolltuiLayoutAction* default_actions, size_t default_actions_n,
                             const RolltuiLayoutHooks* hooks, RolltuiLayoutReport* report);

/* Builds the JSON tree (an OWNED value the caller frees) — `layout_to_json_value`'s port.
 * `base`/`popups` are BORROWS (read-only: this never copies a tree merely to serialise it). */
RolltuiJsonValue* rolltui_layout_to_json_value(const char* name, size_t name_len, int min_width, int min_height,
                                               const RolltuiLayoutAction* actions, size_t actions_n,
                                               const RolltuiLayer* base, const RolltuiLayer* popups,
                                               size_t popups_n, const RolltuiLayoutHooks* hooks);
/* Dumps straight to TEXT, indent 2, REPLACING `*out` — `layout_to_json`'s port. */
void rolltui_layout_to_json_text(const char* name, size_t name_len, int min_width, int min_height,
                                const RolltuiLayoutAction* actions, size_t actions_n, const RolltuiLayer* base,
                                const RolltuiLayer* popups, size_t popups_n, const RolltuiLayoutHooks* hooks,
                                RolltuiStr* out);

/* ---- the stack ------------------------------------------------------------------------------- */

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
int rolltui_window_stack_pop(RolltuiWindowStack* s); /* 0 when only the base remains */
size_t rolltui_window_stack_depth(const RolltuiWindowStack* s);
const RolltuiLayer* rolltui_window_stack_layer(const RolltuiWindowStack* s, size_t i);
int rolltui_window_stack_has_popup(const RolltuiWindowStack* s, const char* id, size_t len);

/* Any layer, any node; NULL when absent. A BORROW valid until the tree is edited. */
RolltuiLayoutNode* rolltui_window_stack_find(const RolltuiWindowStack* s, const char* id, size_t len);

size_t rolltui_window_stack_focus_layer(const RolltuiWindowStack* s);
const RolltuiLayoutNode* rolltui_window_stack_focused(const RolltuiWindowStack* s);
void rolltui_window_stack_focus(RolltuiWindowStack* s, const char* id, size_t len);
void rolltui_window_stack_cycle_focus(RolltuiWindowStack* s, int backwards);

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
const char* rolltui_window_stack_captured(const RolltuiWindowStack* s, size_t* len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_LAYOUT_H */
