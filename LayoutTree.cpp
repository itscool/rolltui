// rolltui/LayoutTree.cpp — the C++ half of `rolltui/c/rolltui_layout_tree.h`: the five
// special members and the four factories, and NOTHING ELSE. Every one of them is a caller of
// the C functions in `c/rolltui_layout_tree.c`, so "release this subtree" has exactly one
// implementation and a destructor is a caller of it rather than a second mechanism.
//
// The C++ edge of a tree the library holds in C (Phase 15 m5).
#include "rolltui/c/rolltui_layout_tree.h"

#include <utility>

// ---- the child list -------------------------------------------------------------------------

RolltuiNodeList::~RolltuiNodeList() { rolltui_node_list_release(this); }

void RolltuiNodeList::copy_from(const RolltuiNodeList& o) { rolltui_node_list_copy(this, &o); }

RolltuiNodeList& RolltuiNodeList::operator=(const RolltuiNodeList& o) {
  if (this != &o) rolltui_node_list_copy(this, &o);
  return *this;
}

RolltuiNodeList& RolltuiNodeList::operator=(RolltuiNodeList&& o) noexcept {
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

void RolltuiNodeList::push_back(RolltuiLayoutNode&& c) {
  RolltuiLayoutNode* p = rolltui_node_list_add(this);
  *p = std::move(c);
}

void RolltuiNodeList::push_back(const RolltuiLayoutNode& c) {
  RolltuiLayoutNode* p = rolltui_node_list_add(this);
  rolltui_layout_node_copy(p, &c);
}

void RolltuiNodeList::insert(const_iterator at, RolltuiLayoutNode&& c) {
  RolltuiLayoutNode* p = rolltui_layout_node_new();
  *p = std::move(c);
  rolltui_node_list_insert(this, static_cast<std::size_t>(at.p - v), p);
}

void RolltuiNodeList::erase(const_iterator at) {
  rolltui_node_list_remove(this, static_cast<std::size_t>(at.p - v));
}

void RolltuiNodeList::clear() { rolltui_node_list_clear(this); }

bool RolltuiNodeList::operator==(const RolltuiNodeList& o) const {
  if (n != o.n) return false;
  for (std::size_t i = 0; i < n; ++i)
    if (!rolltui_layout_node_equal(v[i], o.v[i])) return false;
  return true;
}

// ---- the node -------------------------------------------------------------------------------

RolltuiLayoutNode RolltuiLayoutNode::window(std::string content, RolltuiSplitSize size) {
  RolltuiLayoutNode n;
  n.kind = Kind::Window;
  n.id = content;
  n.content = std::move(content);
  n.size = size;
  return n;
}

RolltuiLayoutNode RolltuiLayoutNode::window_id(std::string id, std::string content, RolltuiSplitSize size) {
  RolltuiLayoutNode n = window(std::move(content), size);
  n.id = std::move(id);
  return n;
}

RolltuiLayoutNode RolltuiLayoutNode::row(std::vector<RolltuiLayoutNode> children, RolltuiSplitSize size) {
  RolltuiLayoutNode n;
  n.kind = Kind::Row;
  for (RolltuiLayoutNode& c : children) n.children.push_back(std::move(c));
  n.size = size;
  return n;
}

RolltuiLayoutNode RolltuiLayoutNode::column(std::vector<RolltuiLayoutNode> children, RolltuiSplitSize size) {
  RolltuiLayoutNode n = row(std::move(children), size);
  n.kind = Kind::Column;
  return n;
}

bool RolltuiLayoutNode::operator==(const RolltuiLayoutNode& o) const {
  return rolltui_layout_node_equal(this, &o) != 0;
}

// ---- a layer ---------------------------------------------------------------------------------
// The five members are written out rather than defaulted because `placement` has to start at
// "the whole of the parent" and a defaulted constructor would start it at nothing — the same
// three fields `rolltui_layer_init` names, in the one place both languages can reach.

RolltuiLayer::RolltuiLayer() {
  placement.w = RolltuiDim::rel(1);
  placement.h = RolltuiDim::rel(1);
  placement.clamp = 1;
}

RolltuiLayer::RolltuiLayer(const RolltuiLayer& o) : RolltuiLayer() { rolltui_layer_copy(this, &o); }

RolltuiLayer::RolltuiLayer(RolltuiLayer&& o) noexcept
    : id(std::move(o.id)),
      placement(o.placement),
      root(std::move(o.root)),
      modal(o.modal),
      focus(std::move(o.focus)) {
  o.modal = 0;
}

RolltuiLayer& RolltuiLayer::operator=(const RolltuiLayer& o) {
  if (this != &o) rolltui_layer_copy(this, &o);
  return *this;
}

RolltuiLayer& RolltuiLayer::operator=(RolltuiLayer&& o) noexcept {
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

RolltuiLayer::~RolltuiLayer() = default;

bool RolltuiLayer::operator==(const RolltuiLayer& o) const { return rolltui_layer_equal(this, &o) != 0; }
