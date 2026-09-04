#pragma once
//
// rolltui/Document.hpp — the library's own document model: what a transcript widget
// renders. Deliberately roll-agnostic (plan/phase-9.md, "Its own document model, not
// roll's"): an ordered list of entries, each a piece of text with a role and an
// optional prefix, rendered as markdown or verbatim. roll's SessionView → Document
// mapping is the adapter's job (milestone 9); a studio can build one from a
// fixture file; a GUI could render the same thing without cells.
//
// `version` is bumped by whoever mutates an entry — its text (a streaming answer) or
// any other field — so the transcript's layout cache, keyed on (id, version, width,
// options), is invalidated by its key, never by an event: the frame stays a pure
// function of state. An entry with a stale version draws stale lines; that is the
// host's bug, and the rule is stated here so it is a rule.
//
// Text is what a renderer will DRAW, never what a terminal will interpret: a host
// whose entries come from an untrusted producer (a model) strips control sequences
// first (unicode::strip_escape_sequences). The wrap engine drops a bare ESC as a
// control, so an unstripped sequence's parameters would show as text — visible, not
// dangerous, since no byte of an entry ever reaches the terminal unre-encoded.
//
// A FOLDABLE entry (a tool call and its result, an approval preview) draws one
// summary line — "▸ summary (N lines)" — while folded and the summary plus its body
// while unfolded. `folded` is the entry's initial state; the transcript widget keeps
// the user's toggles by id. The summary line is chrome: not selectable text. A folded
// entry's contribution to a copied selection is its summary.
//
// AN ENTRY MAY BE IN A STATE (Phase 12 m6, rolltui/Effects.hpp) — the entry awaiting its
// first token, the one being streamed into, the one carrying a fraction. That is ALL a
// document says about motion: `state` is a name, `progress` is a number, and what either
// of them looks like is the theme's. `state` is not part of the layout cache's key
// because it changes nothing about the LINES — the transcript marks the cells it has
// already drawn, so a state that changes mid-stream costs no re-wrap.
#include <cstdint>
#include <string>
#include <vector>

#include "rolltui/Effects.hpp"
#include "rolltui/Style.hpp"
#include "rolltui/c/rolltui_document.h"

namespace rolltui {

// PHASE 15 m5e: `DocEntry` and the entry list ARE the C structs (one definition), because the
// transcript that walks them is behind a C boundary and a HOST fills them — two shapes of an
// entry would be two things a host could disagree with. Nothing a host writes changed:
// `e.text = "..."`, `e.role = Role::warning` and `doc.entries.push_back(std::move(e))` all
// still say what they said. What the port made explicit is that the entries are individually
// allocated and their addresses are STABLE, where `std::vector<DocEntry>` moved every entry's
// four string bookkeepings on each growth and invalidated every pointer anyone held.
using DocEntry = RolltuiDocEntry;

struct Document {
  RolltuiDocument entries;
};

}  // namespace rolltui
