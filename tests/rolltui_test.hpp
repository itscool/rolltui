#pragma once
//
// rolltui_test.hpp — the library's side of the shared test module.
//
// THE HARNESS IS NOT HERE ANY MORE. `check`, `check_quiet`, `report`, the assertion count and
// the report line live in `testkit/testkit.hpp`, which roll's suites include too. This file
// used to be a near-copy of roll's, and the two copies DISAGREED about what a passing test
// means: a suite here that ran zero assertions printed `0 passed, 0 failed — ALL PASS` and
// exited 0, and its report line carried no count the `check_counts` guard could parse, so 44
// binaries were invisible to it. Both are gone by having one definition rather than two.
//
// TESTKIT IS NOT ROLL. It is a leaf at the repository root that includes nothing of either
// side, so `rolltui` still builds and passes with roll's `include/` and `src/` deleted —
// asserted by `rolltui-boundary-test`, which was written before any of this moved and which
// scans this file among the rest.
//
// WHAT IS LEFT HERE is what only the library's own suites need: a session to register kinds
// into, and the std:: bridge over the library's C shapes. The bridge cannot move into the
// module: it is about `RolltuiStr`, so a module holding it would depend on rolltui, and rolltui
// is a CONSUMER of the module.
//
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"
#include "testkit/testkit.hpp"

namespace rolltui_test {

// ONE SESSION FOR A SUITE. A registry is a CONTEXT's now, so a suite that
// registers a widget or effect kind needs a session to register it INTO. Two suites had
// written the same wrapper, which is rule 5 firing, so it lives here once.
//
// It is NOT what a host does, and the difference is the point: a host owns its context and
// frees it where the session ends. A suite has no such place, so the holder frees it at
// static destruction — after the last check has run, which is why no assertion can see it.
// A suite that wants to PROVE a context releases what it holds builds its own and frees it
// itself, in sequence, the way `c_consumer_test` does.
inline RolltuiContext* test_context() {
  struct Holder {
    RolltuiContext* c = rolltui_context_new();
    Holder() = default;
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
    ~Holder() { rolltui_context_free(c); }
  };
  static Holder h;
  return h.c;
}

}  // namespace rolltui_test

// ---- the tests' own bridge to std:: ----------------------------------------------
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
