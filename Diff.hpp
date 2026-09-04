#pragma once
//
// rolltui/Diff.hpp — colouring a unified diff, as a Markdown Highlighter (plan/
// phase-12.md, milestones 5 and 5b).
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
// nothing for a check to fail against. A host that KNOWS its output is a diff says so by
// emitting a ```diff fence. What that means TODAY: roll and the studio both register
// this function, so a MODEL's ```diff fence colours; roll's own `edit_file` approval
// preview does NOT go through here yet — it is a verbatim entry sharing one renderer
// with the plain frontend byte for byte, so putting it under a fence is a change to that
// shared renderer, not to this file. Named rather than implied, because "roll's edit
// preview is coloured" is exactly the kind of thing a reader would otherwise assume.
//
// THE `+`/`-` PREFIX IS THE NON-COLOUR SIGNAL and is never stripped, which is what
// answers `kMustDiffer` for a reader in mono, at 16 colours, or with a colour-vision
// deficiency — the same reasoning that keeps the `▼ N more` marker beside the scrollbar
// in milestone 5a.
//
// ---- WORD LEVEL (milestone 5b) ------------------------------------------------------
//
// m5 built the line level and stopped, because refining a CHANGED PAIR needs a line's
// neighbours and m2's Highlighter was `(lang, line)`. m5b widened that contract to
// `(lang, lines, index)` — see Markdown.hpp for why, and for what deliberately did NOT
// widen — and this is what exercises it. The rules, all stated rather than tuned:
//
//   PAIRING. A maximal run of k removed lines IMMEDIATELY followed by a run of k added
//   lines pairs line i with line i. Runs of unequal length are NOT paired and get line
//   colouring only. There is no similarity score and no best-match search: a pairing
//   nobody can predict from the text is worse than none, and an unpaired hunk still
//   reads correctly — it just reads at the line level, which is where m5 left it.
//
//   REFINEMENT. Both sides are tokenised on UAX #29 word boundaries (the same
//   segmentation double-click uses) and the common LEADING and TRAILING token runs are
//   removed; what is left in the middle takes the `_word` role. When nothing is common
//   at either end the whole line changed, so no word span is emitted — the line role
//   already says everything, and marking the entire line twice is noise.
//
//   BOTH SIDES, ALWAYS — never only the `+`. Colouring one half of a pair reads as a
//   rendering bug, and it was the one option m5's plan entry ruled out by name. The one
//   asymmetry that IS legitimate: when one side's middle is empty (a pure insertion or
//   deletion, where the removed line is a prefix of the added one) that side has no
//   changed run of its OWN to mark. It is not a rendering choice; nothing changed there.
//
//   The spans are emitted non-overlapping — line role, word role, line role — because
//   the renderer resolves overlaps by dropping the later span AND reporting it. A
//   highlighter that needed clamping to look right would be a highlighter that is wrong.
//
// PHASE 15 m2 — THE COLOURISER IS BEHIND A C BOUNDARY (`rolltui/c/rolltui_diff.h`), in one
// of two implementations chosen by `-DROLLTUI_C` (`DiffCpp.cpp` or `c/rolltui_diff.c`).
// Nothing a caller can see changed: `diff_spans` still returns the spans by value, because
// its shape is the `markdown::Highlighter` contract and not this module's choice. What is
// worth knowing is one level down — the boundary reads the BLOCK through an accessor rather
// than being handed an array of lines, so a 100-line diff colours in linear time and copies
// no line, and it is handed the seven ROLES it may emit rather than mirroring the `Role`
// enum in C. That table is in `Diff.cpp`, and it is the prose table below in code.
#include <cstddef>
#include <span>
#include <string>
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
//
// A changed line that PAIRS with its neighbour (see above) is split into three spans so
// its changed word run can take diff_added_word / diff_removed_word.
std::vector<markdown::HighlightSpan> diff_spans(std::string_view lang, std::span<const std::string> lines,
                                                std::size_t index);

}  // namespace rolltui
