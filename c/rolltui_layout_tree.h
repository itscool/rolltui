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
#include "rolltui/c/rolltui_style.h" /* the two role ordinals that cross; see the note there */

#ifdef __cplusplus
#include <cstddef>
#include <string>
#include <string_view>
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
  RolltuiLayerList(const RolltuiLayerList& o) { copy_from(o); }
  RolltuiLayerList(RolltuiLayerList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiLayerList& operator=(const RolltuiLayerList& o) {
    if (this != &o) copy_from(o);
    return *this;
  }
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
  void push_back(const RolltuiLayer& l);
  // Removes the layer whose id matches (a no-op when none does) — Layout.hpp's
  // "remove this popup", the one mutation a host ever asks of this list by name.
  void erase_id(std::string_view id);
  void clear();
  bool operator==(const RolltuiLayerList& o) const;

 private:
  void copy_from(const RolltuiLayerList& o);
#endif
} RolltuiLayerList;

void rolltui_layer_list_release(RolltuiLayerList* l);          /* frees every layer + the array */
void rolltui_layer_list_clear(RolltuiLayerList* l);             /* frees every layer, keeps the array */
void rolltui_layer_list_copy(RolltuiLayerList* to, const RolltuiLayerList* from);
size_t rolltui_layer_list_count(const RolltuiLayerList* l);
RolltuiLayer* rolltui_layer_list_at(const RolltuiLayerList* l, size_t i);
/* Appends an EMPTY layer and returns it — the C's `emplace_back`. */
RolltuiLayer* rolltui_layer_list_add(RolltuiLayerList* l);
void rolltui_layer_list_remove(RolltuiLayerList* l, size_t i); /* frees it, shifts the rest down */
void rolltui_layer_list_remove_id(RolltuiLayerList* l, const char* id, size_t len); /* no-op if absent */
int rolltui_layer_list_equal(const RolltuiLayerList* a, const RolltuiLayerList* b);

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

inline void RolltuiNodeList::copy_from(const RolltuiNodeList& o) { rolltui_node_list_copy(this, &o); }

inline RolltuiNodeList& RolltuiNodeList::operator=(const RolltuiNodeList& o) {
  if (this != &o) rolltui_node_list_copy(this, &o);
  return *this;
}

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

// ---- the node -------------------------------------------------------------------------------

inline RolltuiLayoutNode RolltuiLayoutNode::window(std::string content, RolltuiSplitSize size) {
  RolltuiLayoutNode n;
  n.kind = Kind::Window;
  n.id = content;
  n.content = std::move(content);
  n.size = size;
  return n;
}

inline RolltuiLayoutNode RolltuiLayoutNode::window_id(std::string id, std::string content, RolltuiSplitSize size) {
  RolltuiLayoutNode n = window(std::move(content), size);
  n.id = std::move(id);
  return n;
}

inline RolltuiLayoutNode RolltuiLayoutNode::row(std::vector<RolltuiLayoutNode> children, RolltuiSplitSize size) {
  RolltuiLayoutNode n;
  n.kind = Kind::Row;
  for (RolltuiLayoutNode& c : children) n.children.push_back(std::move(c));
  n.size = size;
  return n;
}

inline RolltuiLayoutNode RolltuiLayoutNode::column(std::vector<RolltuiLayoutNode> children, RolltuiSplitSize size) {
  RolltuiLayoutNode n = row(std::move(children), size);
  n.kind = Kind::Column;
  return n;
}

inline bool RolltuiLayoutNode::operator==(const RolltuiLayoutNode& o) const {
  return rolltui_layout_node_equal(this, &o) != 0;
}

// ---- a layer ---------------------------------------------------------------------------------
// The five members are written out rather than defaulted because `placement` has to start at
// "the whole of the parent" and a defaulted constructor would start it at nothing — the same
// three fields `rolltui_layer_init` names, in the one place both languages can reach.

inline RolltuiLayer::RolltuiLayer() {
  placement.w = RolltuiDim::rel(1);
  placement.h = RolltuiDim::rel(1);
  placement.clamp = 1;
}

inline RolltuiLayer::RolltuiLayer(const RolltuiLayer& o) : RolltuiLayer() { rolltui_layer_copy(this, &o); }

inline RolltuiLayer::RolltuiLayer(RolltuiLayer&& o) noexcept
    : id(std::move(o.id)),
      placement(o.placement),
      root(std::move(o.root)),
      modal(o.modal),
      focus(std::move(o.focus)) {
  o.modal = 0;
}

inline RolltuiLayer& RolltuiLayer::operator=(const RolltuiLayer& o) {
  if (this != &o) rolltui_layer_copy(this, &o);
  return *this;
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

// ---- popups: an owned array of Layer values ---------------------------------------------------

inline RolltuiLayerList::~RolltuiLayerList() { rolltui_layer_list_release(this); }

inline void RolltuiLayerList::copy_from(const RolltuiLayerList& o) { rolltui_layer_list_copy(this, &o); }

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

inline void RolltuiLayerList::push_back(const RolltuiLayer& l) { rolltui_layer_copy(rolltui_layer_list_add(this), &l); }

inline void RolltuiLayerList::erase_id(std::string_view id) { rolltui_layer_list_remove_id(this, id.data(), id.size()); }

inline void RolltuiLayerList::clear() { rolltui_layer_list_clear(this); }

inline bool RolltuiLayerList::operator==(const RolltuiLayerList& o) const { return rolltui_layer_list_equal(this, &o) != 0; }
#endif /* __cplusplus */

#endif /* ROLLTUI_C_LAYOUT_TREE_H */
