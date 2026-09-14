#ifndef ROLLTUI_C_LAYOUT_TREE_H
#define ROLLTUI_C_LAYOUT_TREE_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h`. What is below is
 * the library's own — reached by its `.c` files, and by a suite that opts in by including this
 * header by name. */
/*
 * rolltui/c/rolltui_layout_tree.h — THE SPLIT TREE, AS DATA.
 *
 * A window, a row, a column, a placed layer. What each one MEANS is stated in
 * `rolltui/Layout.hpp` and asserted in `rolltui/tests/layout_test.cpp`; none of it is
 * repeated here. This file answers one question the C++ never had to: **who owns a
 * child?**
 *
 * ---- WHY THIS IS IN BOTH CONFIGURATIONS -----------------------------------------------------
 *
 * The same reason `rolltui_md_lines.c` is: **it is DATA both implementations
 * of the placement algorithm fill and read**, and two copies of a data structure are two
 * things that can disagree about what a node is. The flag chooses the ALGORITHM
 * (`rolltui_layout.h`), never the shape of what it walks.
 *
 * ---- THE LIFETIME THE C++ LEFT IMPLICIT, WHICH IS THIS MILESTONE'S SUBJECT ------------------
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
 * ---- ONE DEFINITION -------------------------------------------------------------------------
 *
 * `rolltui::Node`, `rolltui::Layer`, `rolltui::Dim`, `rolltui::Placement` and
 * `rolltui::SplitSize` ARE these structs. The C++ side adds the five
 * special members and the two dozen accessors host code already writes, and every one of
 * them calls the C functions below — one implementation of "release this subtree", with the
 * destructor as a caller of it.
 */

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Takes ownership of `n`. */
void rolltui_node_list_push(RolltuiNodeList* l, RolltuiLayoutNode* n);

void rolltui_layer_list_remove(RolltuiLayerList* l, size_t i); /* frees it, shifts the rest down */

/* ---- INTERNAL: not part of the public API ---------------------------------------------------
 * Reached by the library's own `.c` files, by rolltui's authoring tool, or by a suite that
 * tests this module's implementation — never by a host. The library does not promise these,
 * so their shape can change without breaking a consumer. */
void rolltui_layout_node_release(RolltuiLayoutNode* n);

/* Releases and frees. A no-op on NULL. */
void rolltui_layout_node_free(RolltuiLayoutNode* n);

void rolltui_node_list_copy(RolltuiNodeList* to, const RolltuiNodeList* from);

void rolltui_layer_init(RolltuiLayer* l);
void rolltui_layer_release(RolltuiLayer* l);

void rolltui_layer_list_copy(RolltuiLayerList* to, const RolltuiLayerList* from);

#ifdef __cplusplus
void rolltui_layout_copy(RolltuiLayout* to, const RolltuiLayout* from); /* the handle's deep copy */

/* Internal, with the types they operate on. */
void rolltui_layout_node_init(RolltuiLayoutNode* n);
void rolltui_layout_node_copy(RolltuiLayoutNode* to, const RolltuiLayoutNode* from);
int rolltui_layout_node_equal(const RolltuiLayoutNode* a, const RolltuiLayoutNode* b);
RolltuiLayoutNode* rolltui_layout_node_new(void);
RolltuiLayoutNode* rolltui_node_list_add(RolltuiNodeList* l);
void rolltui_node_list_insert(RolltuiNodeList* l, size_t i, RolltuiLayoutNode* n);
void rolltui_layer_copy(RolltuiLayer* to, const RolltuiLayer* from);
void rolltui_layer_move(RolltuiLayer* to, RolltuiLayer* from);
int rolltui_layer_equal(const RolltuiLayer* a, const RolltuiLayer* b);
RolltuiLayer* rolltui_layer_list_add(RolltuiLayerList* l);
int rolltui_layer_list_equal(const RolltuiLayerList* a, const RolltuiLayerList* b);
void rolltui_action_list_release(RolltuiActionList* l);
int rolltui_action_list_equal(const RolltuiActionList* a, const RolltuiActionList* b);
void rolltui_action_list_clear(RolltuiActionList* l);
RolltuiLayoutAction* rolltui_action_list_add(RolltuiActionList* l);

void rolltui_node_list_remove(RolltuiNodeList* l, size_t i); /* frees it */
void rolltui_node_list_clear(RolltuiNodeList* l);            /* frees every child */
void rolltui_node_list_release(RolltuiNodeList* l);          /* …and the array */
void rolltui_layer_list_release(RolltuiLayerList* l);          /* frees every layer + the array */
void rolltui_layer_list_clear(RolltuiLayerList* l);             /* frees every layer, keeps the array */
void rolltui_layer_list_remove_id(RolltuiLayerList* l, const char* id, size_t len); /* no-op if absent */
void rolltui_action_list_remove_name(RolltuiActionList* l, const char* name, size_t len); /* no-op if absent */
void rolltui_window_stack_push(RolltuiWindowStack* s, RolltuiLayer* popup);
int rolltui_layout_equal(const RolltuiLayout* a, const RolltuiLayout* b);
} /* extern "C" */
#endif

/* ==========================================================================================
 * THE LAYOUT FAMILY'S STRUCTURES. `rolltui/rolltui.h` declares these types as opaque handles
 * and publishes seven accessors; everything below is the library's own
 * and the studio's layout editor, which is the only consumer that walks and mutates a tree.
 * ========================================================================================== */
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

/* A node's KIND, spelled once. C++ has `RolltuiLayoutNode::Kind`; C had the bare integers, and
 * without it three files wrote a literal 0 with the word "Window" in a trailing comment from
 * memory, which is the duplication rule firing on a magic number rather than on a word. */
#define ROLLTUI_NODE_WINDOW 0
#define ROLLTUI_NODE_ROW 1
#define ROLLTUI_NODE_COLUMN 2

typedef struct RolltuiLayoutNode {
#ifdef __cplusplus
  enum class Kind : unsigned char { Window = 0, Row, Column };
  Kind kind = Kind::Window;
#else
  unsigned char kind; /* ROLLTUI_NODE_WINDOW | _ROW | _COLUMN */
#endif
  RolltuiStr id;      /* defaults to `content` for windows; optional on splits */
  RolltuiStr content; /* windows: "kind[:source]" */
#ifdef __cplusplus
  rolltui::Border border = rolltui::Border::None;
#else
  unsigned char border;
#endif
  RolltuiStr title;
  /* What the WIDGET in this window says it is called, this frame — a menu's breadcrumb — set by
   * `rolltui_windows_autosize` from the widget's `title` slot and drawn instead of `title` while
   * it is non-empty. Screen state, not layout: never read from a file, never written to one,
   * never compared, never copied with the node. */
  RolltuiStr live_title;
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
  //. A container node takes its children by `children.push_back(std::move(c))`.
  static RolltuiLayoutNode window(const char* content, RolltuiSplitSize size = {});
  static RolltuiLayoutNode window_id(const char* id, const char* content, RolltuiSplitSize size = {});
  static RolltuiLayoutNode row(RolltuiSplitSize size = {});
  static RolltuiLayoutNode column(RolltuiSplitSize size = {});
#endif
} RolltuiLayoutNode;

typedef struct RolltuiLayer {
  RolltuiStr id; /* a popup's name in the layout file; "" for the base */
  RolltuiPlacement placement;
  RolltuiLayoutNode root;
  unsigned char modal ROLLTUI_DEFAULT(0);
  RolltuiStr focus; /* the focused window id; "" → first focusable in tree order */

#ifdef __cplusplus
  RolltuiLayer();
  // COPY IS DELETED: `RolltuiLayer copy = *p;` is a deep copy in C++ and a shallow alias in
  // C, so the identical line double-frees. The spelling is `clone()`, which is
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

/* ---- forward declarations the C++ members just below call ---------------------------------*/
void rolltui_layer_copy(RolltuiLayer* to, const RolltuiLayer* from);
int rolltui_layer_equal(const RolltuiLayer* a, const RolltuiLayer* b);
RolltuiLayer* rolltui_layer_list_add(RolltuiLayerList* l);
void rolltui_layer_list_clear(RolltuiLayerList* l);
int rolltui_layer_list_equal(const RolltuiLayerList* a, const RolltuiLayerList* b);
void rolltui_layer_list_release(RolltuiLayerList* l);
void rolltui_layer_list_remove_id(RolltuiLayerList* l, const char* id, size_t len);
void rolltui_layer_move(RolltuiLayer* to, RolltuiLayer* from);
void rolltui_layout_node_copy(RolltuiLayoutNode* to, const RolltuiLayoutNode* from);
int rolltui_layout_node_equal(const RolltuiLayoutNode* a, const RolltuiLayoutNode* b);
RolltuiLayoutNode* rolltui_layout_node_new(void);
RolltuiLayoutNode* rolltui_node_list_add(RolltuiNodeList* l);
void rolltui_node_list_clear(RolltuiNodeList* l);
void rolltui_node_list_insert(RolltuiNodeList* l, size_t i, RolltuiLayoutNode* n);
void rolltui_node_list_release(RolltuiNodeList* l);
void rolltui_node_list_remove(RolltuiNodeList* l, size_t i);

#ifdef __cplusplus
/* ---- the C++ special members of the structs above -------------------------------------------
 * Each one is a CALLER of a C function declared above it, so "release this subtree" has one
 * implementation and a destructor reaches it rather than being a second mechanism.
 *
 * They were out-of-line in `rolltui/LayoutTree.cpp` until the C++ binding was deleted. They are not
 * part of that binding — they are what makes "the C++ type IS the C struct" true (the
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


typedef struct RolltuiContent {
  RolltuiStr kind;   /* the kind's name — its identity, whichever rung it resolves at */
  RolltuiStr source; /* the part after the first ':' — a bound name, a literal, a path */
#ifdef __cplusplus
  bool operator==(const RolltuiContent&) const = default;
#endif
} RolltuiContent;

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

typedef struct RolltuiLoadedLayout {
  RolltuiStr name;
  int min_width, min_height;
  RolltuiLayoutAction* actions;
  size_t actions_n, actions_cap;
  RolltuiLayer base;
  RolltuiLayer* popups;
  size_t popups_n, popups_cap;
} RolltuiLoadedLayout;

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
  // The explicit copy: `rolltui_layout_copy`, spelled at the call site. Copying
  // by `=` is deleted through every member, which is the point.
  RolltuiLayout clone() const;
#endif
} RolltuiLayout;

RolltuiLayoutAction* rolltui_action_list_add(RolltuiActionList* l);
void rolltui_action_list_clear(RolltuiActionList* l);
int rolltui_action_list_equal(const RolltuiActionList* a, const RolltuiActionList* b);
void rolltui_action_list_release(RolltuiActionList* l);
void rolltui_action_list_remove_name(RolltuiActionList* l, const char* name, size_t len);

#ifdef __cplusplus
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
#endif /* __cplusplus */

#endif /* ROLLTUI_C_LAYOUT_TREE_H */
