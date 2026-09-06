// rolltui/tools/tool_str.hpp — the tools' OWN bridge from rolltui's C shapes to the std:: types
// the studio and the editors compose with (Phase 19 m2). One file for one application (the
// studio, paint and the three editors ship together); it is not the library's and must not
// become a second one.
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"

#ifndef ROLLTUI_STD_BRIDGE_DEFINED
#define ROLLTUI_STD_BRIDGE_DEFINED
inline std::string str_of(const RolltuiStr& s) { return std::string(s.p ? s.p : "", s.n); }
inline std::string_view view_of(const RolltuiStr& s) { return std::string_view(s.p ? s.p : "", s.n); }

inline void set_str(RolltuiStr& s, std::string_view v) { rolltui_str_set(&s, v.data(), v.size()); }
inline void set_note(RolltuiNote& n, std::string_view v) { n.set(v.data(), v.size()); }
// The tree shapes the tools and tests compose in a std::vector of their own, then hand to a
// rolltui item — the consumer's container, the library's items.
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
#endif  /* ROLLTUI_STD_BRIDGE_DEFINED */
