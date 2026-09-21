// rolltui/tools/tool_str.hpp — the tools' menu-tree builders. The studio and its editors compose a menu as a std::vector of items,
// then hand it to a rolltui item: the consumer's container, the library's items. Strings are not here: the studio and its editors
// hold `RolltuiStr`, `StrView` and `StrVec` (`rolltui/str.hpp`) from the library's calls to their own logic.
#pragma once
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"

#ifndef ROLLTUI_TREE_BRIDGE_DEFINED
#define ROLLTUI_TREE_BRIDGE_DEFINED
inline RolltuiMenuItem submenu_of(const char* id, const char* label, std::vector<RolltuiMenuItem>&& children) {
  RolltuiMenuItem it = RolltuiMenuItem::submenu(id, label);
  for (RolltuiMenuItem& c : children) it.children.push_back(std::move(c));
  return it;
}
inline RolltuiMenuItem choice_of(const char* id, const char* label, std::vector<RolltuiMenuItem>&& options, const char* value) {
  RolltuiMenuItem it = RolltuiMenuItem::choice(id, label, value);
  for (RolltuiMenuItem& c : options) it.children.push_back(std::move(c));
  return it;
}
inline std::vector<RolltuiMenuItem> clone_items(const std::vector<RolltuiMenuItem>& v) {
  std::vector<RolltuiMenuItem> out;
  out.reserve(v.size());
  for (const RolltuiMenuItem& c : v) out.push_back(c.clone());
  return out;
}
#endif  /* ROLLTUI_TREE_BRIDGE_DEFINED */
