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

namespace rolltui {

struct DocEntry {
  std::string id;          // stable identity across frames
  std::uint64_t version = 0;
  std::string text;        // markdown source, or verbatim text
  bool markdown = true;    // false: rendered as plain wrapped lines
  Role role = Role::text;  // base role for the entry's text
  std::string prefix;      // drawn before the first line (e.g. "> " for a prompt), in `prefix_role`
  Role prefix_role = Role::prompt;
  bool foldable = false;
  std::string summary;     // the one-line summary a foldable entry shows
  bool folded = true;      // initial fold state of a foldable entry
  // Motion (see above). None — the default — is a still entry under every theme.
  EffectState state = EffectState::None;
  double progress = 0;     // EffectState::Progress: 0..1
  std::uint64_t state_since_ms = 0;  // when it entered `state`; 0 → the shared clock's phase
};

struct Document {
  std::vector<DocEntry> entries;
};

}  // namespace rolltui
