#pragma once
//
// rolltui_test.hpp — the whole test harness for the rolltui library. Deliberately the
// same shape as roll's tests/test_util.hpp and deliberately not that file: the
// library's tests include nothing of roll's, so that "rolltui builds and passes with
// roll's include/ absent" (phase 9 Done-when (g)) is a property of the build, not a
// promise.
//
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"

namespace rolltui_test {

inline int g_fail = 0;
inline int g_pass = 0;

inline void check(bool cond, const std::string& name) {
  std::fprintf(stdout, "  [%s] %s\n", cond ? "PASS" : "FAIL", name.c_str());
  if (cond) ++g_pass; else ++g_fail;
}

// For suites with thousands of cases: count silently, print only failures.
inline void check_quiet(bool cond, const std::string& name) {
  if (cond) { ++g_pass; return; }
  ++g_fail;
  std::fprintf(stdout, "  [FAIL] %s\n", name.c_str());
}

inline int report(const char* suite) {
  std::fprintf(stdout, "\n%s: %d passed, %d failed — %s\n", suite, g_pass, g_fail,
               g_fail == 0 ? "ALL PASS" : "FAILURES");
  return g_fail == 0 ? 0 : 1;
}

}  // namespace rolltui_test

// ---- the tests' own bridge to std:: (Phase 19 m2) ----------------------------------------------
// The library's C++ shape names no std:: container or view; what a test wants as a std::string
// or string_view it makes HERE, in its own harness, which is what "in your own file" means.
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
inline std::string_view kind_of(const RolltuiEffectSpec& s) { return std::string_view(s.kind, s.kind_len); }
inline std::string_view frame_of(const RolltuiEffectSpec& s, std::size_t i) {
  return i < s.frame_count ? std::string_view(s.frames[i].bytes, s.frames[i].len) : std::string_view();
}
