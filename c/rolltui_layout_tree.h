#ifndef ROLLTUI_C_LAYOUT_TREE_H
#define ROLLTUI_C_LAYOUT_TREE_H
/*
 * rolltui/c/rolltui_layout_tree.h — THE SPLIT TREE, AS DATA (Phase 15 m5).
 *
 * A window, a row, a column, a placed layer. What each one MEANS is stated in
 * `rolltui/Layout.hpp` and asserted in `rolltui/tests/layout_test.cpp`; none of it is
 * repeated here. This file answers one question the C++ never had to: **who owns a
 * child?**
 *
 * ---- WHY THIS IS IN BOTH CONFIGURATIONS ------------------------------------------------
 *
 * The same reason `rolltui_md_lines.c` is (Phase 15 m4): **it is DATA both implementations
 * of the placement algorithm fill and read**, and two copies of a data structure are two
 * things that can disagree about what a node is. The flag chooses the ALGORITHM
 * (`rolltui_layout.h`), never the shape of what it walks.
 *
 * ---- THE LIFETIME THE C++ LEFT IMPLICIT, WHICH IS THIS MILESTONE'S SUBJECT --------------
 *
 * `std::vector<Node> children` is a recursive owning tree, and nobody ever decided that:
 * it is what you type. Four consequences nobody was asked about either —
 *
 *   1. **`WindowStack::set_base(const Layer&)` DEEP-COPIES A WHOLE TREE** on every layout
 *      hot-reload, and `push(Layer)` takes one BY VALUE, so opening a popup copies its
 *      tree twice on the way in. Three `std::string`s and a vector per node, per copy.
 *   2. **A node is not stable.** `children.push_back` reallocates, so every `const Node*`
 *      into a sibling array — which is what `place()` and `find()` hand out — is a pointer
 *      into storage a later edit may move. It has never bitten because nothing edits during
 *      a resolve, which is a fact about call order rather than about the type.
 *   3. **`operator==` is a deep tree compare** the compiler wrote, used by the preset
 *      store's "(modified)" check on every keystroke of the design editor.
 *   4. **Every node carries three `std::string`s** whose bytes are almost always under
 *      sixteen and therefore never reach the heap — which is exactly why nobody looked.
 *
 * The C answers all four out loud: a child is an OWNED, INDIVIDUALLY ALLOCATED node whose
 * address never moves, the list holds pointers, and copying a tree is a function with a
 * name (`rolltui_layout_node_copy`) rather than a `=`.
 *
 * ---- ONE DEFINITION ---------------------------------------------------------------------
 *
 * `rolltui::Node`, `rolltui::Layer`, `rolltui::Dim`, `rolltui::Placement` and
 * `rolltui::SplitSize` ARE these structs, the Phase 14 rule. The C++ side adds the five
 * special members and the two dozen accessors host code already writes, and every one of
 * them calls the C functions below — one implementation of "release this subtree", with the
 * destructor as a caller of it.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_geom.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
#include <cstddef>
#include <string>
#include <vector>
#endif

/* The enums both languages use. Outside `extern "C"` because a C++ scoped enum is what
 * every existing call site writes (`Border::Single`), and the fixed underlying type is what
 * makes the two spellings one byte by the standard rather than by convention — the same
 * trade `RolltuiMouseEvent::Kind` already made. */
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

/* THE ONE ROLE ORDINAL THAT CROSSES, and it is CHECKED rather than trusted: `Style.hpp`
 * static_asserts that it is `Role::background`, so the two cannot drift. Naming the ordinal
 * and not the role is what keeps the vocabulary in one file (m2's rule) while still letting
 * a node have a sensible default in a language with no `Role`. */
#define ROLLTUI_ROLE_DEFAULT_BACKGROUND 2

#ifdef __cplusplus
extern "C" {
#endif

/* ---- dimensions ------------------------------------------------------------------------- */

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

/* ---- the node --------------------------------------------------------------------------- */

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
  RolltuiNodeList(const RolltuiNodeList& o) { copy_from(o); }
  RolltuiNodeList(RolltuiNodeList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiNodeList& operator=(const RolltuiNodeList& o);
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
  void copy_from(const RolltuiNodeList& o);
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
  static RolltuiLayoutNode window(std::string content, RolltuiSplitSize size = {});
  static RolltuiLayoutNode window_id(std::string id, std::string content, RolltuiSplitSize size = {});
  static RolltuiLayoutNode row(std::vector<RolltuiLayoutNode> children, RolltuiSplitSize size = {});
  static RolltuiLayoutNode column(std::vector<RolltuiLayoutNode> children, RolltuiSplitSize size = {});
  bool operator==(const RolltuiLayoutNode& o) const;
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

/* ---- the child list, for C callers ------------------------------------------------------- */
size_t rolltui_node_list_count(const RolltuiNodeList* l);
RolltuiLayoutNode* rolltui_node_list_at(const RolltuiNodeList* l, size_t i);
/* Appends an EMPTY child and returns it — the C's `emplace_back`, so a caller never builds a
 * node on the stack and copies it in. */
RolltuiLayoutNode* rolltui_node_list_add(RolltuiNodeList* l);
/* Takes ownership of `n`. */
void rolltui_node_list_push(RolltuiNodeList* l, RolltuiLayoutNode* n);
void rolltui_node_list_insert(RolltuiNodeList* l, size_t i, RolltuiLayoutNode* n);
void rolltui_node_list_remove(RolltuiNodeList* l, size_t i); /* frees it */
void rolltui_node_list_clear(RolltuiNodeList* l);            /* frees every child */
void rolltui_node_list_release(RolltuiNodeList* l);          /* …and the array */
void rolltui_node_list_copy(RolltuiNodeList* to, const RolltuiNodeList* from);

/* ---- a layer ----------------------------------------------------------------------------- */

typedef struct RolltuiLayer {
  RolltuiStr id; /* a popup's name in the layout file; "" for the base */
  RolltuiPlacement placement;
  RolltuiLayoutNode root;
  unsigned char modal ROLLTUI_DEFAULT(0);
  RolltuiStr focus; /* the focused window id; "" → first focusable in tree order */

#ifdef __cplusplus
  RolltuiLayer();
  RolltuiLayer(const RolltuiLayer& o);
  RolltuiLayer(RolltuiLayer&& o) noexcept;
  RolltuiLayer& operator=(const RolltuiLayer& o);
  RolltuiLayer& operator=(RolltuiLayer&& o) noexcept;
  ~RolltuiLayer();
  bool operator==(const RolltuiLayer& o) const;
#endif
} RolltuiLayer;

void rolltui_layer_init(RolltuiLayer* l);
void rolltui_layer_release(RolltuiLayer* l);
void rolltui_layer_copy(RolltuiLayer* to, const RolltuiLayer* from);
/* Takes `from`'s buffers and leaves it empty — the move, written down for C. */
void rolltui_layer_move(RolltuiLayer* to, RolltuiLayer* from);
int rolltui_layer_equal(const RolltuiLayer* a, const RolltuiLayer* b);

/* ---- what a resolve produces --------------------------------------------------------------- */

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
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_LAYOUT_TREE_H */
