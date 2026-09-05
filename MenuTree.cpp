// rolltui/MenuTree.cpp — the C++ half of `rolltui/c/rolltui_menu_tree.h`: the special
// members and the six factories, and NOTHING ELSE. Every one of them is a caller of the C
// functions in `c/rolltui_menu_tree.c`, so "release this subtree" has one implementation and
// a destructor is a caller of it (Phase 15 m5). The C++ edge of a tree the library holds in C.
#include "rolltui/c/rolltui_menu_tree.h"

#include <utility>

// ---- the spec ---------------------------------------------------------------------------

bool RolltuiInputSpec::operator==(const RolltuiInputSpec& o) const {
  return rolltui_input_spec_equal(this, &o) != 0;
}

// ---- the child list ---------------------------------------------------------------------

RolltuiMenuItemList::RolltuiMenuItemList(const RolltuiMenuItemList& o) { rolltui_menu_list_copy(this, &o); }

RolltuiMenuItemList::~RolltuiMenuItemList() { rolltui_menu_list_release(this); }

RolltuiMenuItemList& RolltuiMenuItemList::operator=(const RolltuiMenuItemList& o) {
  if (this != &o) rolltui_menu_list_copy(this, &o);
  return *this;
}

RolltuiMenuItemList& RolltuiMenuItemList::operator=(RolltuiMenuItemList&& o) noexcept {
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

void RolltuiMenuItemList::push_back(RolltuiMenuItem&& c) { *rolltui_menu_list_add(this) = std::move(c); }

void RolltuiMenuItemList::push_back(const RolltuiMenuItem& c) {
  rolltui_menu_item_copy(rolltui_menu_list_add(this), &c);
}

void RolltuiMenuItemList::clear() { rolltui_menu_list_clear(this); }

bool RolltuiMenuItemList::operator==(const RolltuiMenuItemList& o) const {
  if (n != o.n) return false;
  for (std::size_t i = 0; i < n; ++i)
    if (!rolltui_menu_item_equal(v[i], o.v[i])) return false;
  return true;
}

// ---- the item ---------------------------------------------------------------------------

RolltuiMenuItem::RolltuiMenuItem() = default;

bool RolltuiMenuItem::operator==(const RolltuiMenuItem& o) const {
  return rolltui_menu_item_equal(this, &o) != 0;
}

RolltuiMenuItem RolltuiMenuItem::action(std::string id, std::string label, std::string shortcut) {
  RolltuiMenuItem it;
  it.kind = Kind::Action;
  it.id = std::move(id);
  it.label = std::move(label);
  it.shortcut = std::move(shortcut);
  return it;
}

RolltuiMenuItem RolltuiMenuItem::submenu(std::string id, std::string label,
                                         std::vector<RolltuiMenuItem> children) {
  RolltuiMenuItem it;
  it.kind = Kind::Submenu;
  it.id = std::move(id);
  it.label = std::move(label);
  for (RolltuiMenuItem& c : children) it.children.push_back(std::move(c));
  return it;
}

RolltuiMenuItem RolltuiMenuItem::toggle(std::string id, std::string label, bool checked) {
  RolltuiMenuItem it;
  it.kind = Kind::Toggle;
  it.id = std::move(id);
  it.label = std::move(label);
  it.checked = static_cast<unsigned char>(checked);
  return it;
}

RolltuiMenuItem RolltuiMenuItem::choice(std::string id, std::string label,
                                        std::vector<RolltuiMenuItem> options, std::string value) {
  RolltuiMenuItem it = submenu(std::move(id), std::move(label), std::move(options));
  it.kind = Kind::Choice;
  it.value = std::move(value);
  return it;
}

RolltuiMenuItem RolltuiMenuItem::input(std::string id, std::string label, std::string value) {
  RolltuiMenuItem it;
  it.kind = Kind::Input;
  it.id = std::move(id);
  it.label = std::move(label);
  it.value = std::move(value);
  return it;
}

RolltuiMenuItem RolltuiMenuItem::input(std::string id, std::string label, RolltuiInputSpec spec,
                                       std::string value) {
  RolltuiMenuItem it = input(std::move(id), std::move(label), std::move(value));
  it.spec = std::move(spec);
  return it;
}
