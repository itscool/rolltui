#pragma once
//
// rolltui/Diff.hpp — colouring a unified diff, as a Markdown Highlighter (plan/
// phase-12.md, milestone 5b).
//
// WHY THIS IS IN THE LIBRARY when milestone 2 says the library ships no highlighter.
// The thing m2 excluded is a SYNTAX highlighter: a language grammar, an ecosystem
// integration (tree-sitter), and a moving target per language. This is three rules on a
// line's first byte. It belongs here for the same reason `diff_added`/`diff_removed`/
// `diff_context` are Roles rather than two colours somebody hardcoded: `ThemeAnalysis`
// already checks that pair is distinguishable under every colour-vision simulation, and
// a role nothing emits is a check standing on nothing.
//
// THE FENCE DECLARES IT, CONTENT IS NEVER SNIFFED (milestone 5b). This function is
// registered by a host and answers only for the language tags below; everything else
// gets no spans and renders plain. A "this looks like a diff" heuristic would paint a
// Python file's leading `-` lines red the first day someone pasted one, which is this
// project's exact recurring failure — a wrong branch taken with full confidence and
// nothing for a check to fail against. A host that KNOWS its output is a diff (roll's
// own `edit_file` preview) says so by emitting a ```diff fence.
//
// THE `+`/`-` PREFIX IS THE NON-COLOUR SIGNAL and is never stripped, which is what
// answers `kMustDiffer` for a reader in mono, at 16 colours, or with a colour-vision
// deficiency — the same reasoning that keeps the `▼ N more` marker beside the scrollbar
// in milestone 5a.
//
// WHAT IT DOES NOT DO YET: word-level colouring inside a changed line PAIR. That needs a
// line's NEIGHBOURS, and m2's Highlighter contract is `(lang, line)` — one line, no
// context, deliberately minimal. Colouring only the `+` side (the one a stateful
// callback could reach by remembering the previous line) would be asymmetric, and
// widening the contract is a decision about m2's seam rather than a detail of this
// function. Stated here and in plan/phase-12.md rather than half-built.
//
#include <string_view>
#include <vector>

#include "rolltui/Markdown.hpp"

namespace rolltui {

// True for the info strings this colouriser answers to: "diff", "patch", "udiff".
bool is_diff_language(std::string_view lang);

// One line of a unified diff → the spans its role needs. Empty for a language this does
// not claim, and for a line that carries no diff meaning.
//
//   +…            diff_added        -…            diff_removed
//   @@ …          accent_1 (a hunk header: a position, not a change)
//   --- / +++     text_muted        (the file headers, which are not add/remove lines)
//   ' ' or empty  diff_context
// A `\ No newline at end of file` line is context: it says something about the line
// above, and colouring it as a change would be a lie about the file.
std::vector<markdown::HighlightSpan> diff_spans(std::string_view lang, std::string_view line);

}  // namespace rolltui
