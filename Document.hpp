#pragma once
//
// rolltui/Document.hpp — the library's own document model: what a transcript widget
// renders. Deliberately roll-agnostic (plan/phase-9.md, "Its own document model, not
// roll's"): an ordered list of entries, each a piece of text with a role and an
// optional prefix, rendered as markdown or verbatim. roll's SessionView → Document
// mapping is the adapter's job (milestone 9); a playground can build one from a
// fixture file; a GUI could render the same thing without cells.
//
// `version` is bumped by whoever mutates an entry's text (a streaming answer), so a
// layout cache keyed on (id, version, width, theme) is invalidated by its key, never
// by an event — which keeps the frame a pure function of state.
//
#include <cstdint>
#include <string>
#include <vector>

#include "rolltui/Style.hpp"

namespace rolltui {

struct DocEntry {
  std::string id;          // stable identity across frames
  std::uint64_t version = 0;
  std::string text;        // markdown source, or verbatim text
  bool markdown = true;    // false: rendered as plain wrapped lines
  Role role = Role::text;  // base role for the entry's text
  std::string prefix;      // drawn before the first line (e.g. "> " for a prompt), in `prefix_role`
  Role prefix_role = Role::prompt;
};

struct Document {
  std::vector<DocEntry> entries;
};

}  // namespace rolltui
