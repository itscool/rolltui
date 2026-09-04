#pragma once
//
// rolltui/Markdown.hpp — CommonMark + GFM (tables, strikethrough, task lists,
// autolinks) parsed by vendored md4c into OUR block tree, then rendered into styled,
// wrapped lines. md4c is an implementation detail of Markdown.cpp; nothing outside
// that file sees an MD_ type.
//
//   Document parse(std::string_view source)
//   std::vector<StyledLine> render(const Document&, const RenderOptions&)
//   std::vector<StyledLine> render(std::string_view source, const RenderOptions&)
//
// Blocks: heading (level), paragraph, fenced/indented code (info string kept), list
// (ordered/unordered/task, nested, tight/loose), blockquote, table, thematic break,
// HTML block (rendered as code — never interpreted). Inlines: emphasis, strong, code
// span, link (text shown, URL shown after it in md_link_url), image (as a link),
// strikethrough, autolink, hard/soft breaks, entities, NUL → U+FFFD.
//
// Guarantees (asserted in rolltui/tests/markdown_test.cpp):
//   - The renderer never drops a character of the source text: the plain text of the
//     rendered lines contains, in order, every non-space grapheme md4c reported. It
//     ADDS chrome (markers, borders, URLs) and drops only wrap-time spaces.
//   - No rendered line exceeds the width, except the wrap engine's stated single-
//     oversized-grapheme case.
//   - An unterminated fence renders as a code block (CommonMark: it runs to the end
//     of the document) — the right picture while the fence is still arriving.
//   - A table whose minimum (one cell per column plus borders, 3·cols+1) exceeds the
//     width is rendered as its pipe-table source in a code block — a guarantee, not a
//     warning; silently dropping columns is the wrong display that looks fine.
//   - Inline styles survive wrapping: the paragraph is wrapped as one string and each
//     drawn grapheme takes the role of the run its source offset came from.
//   - No syntax highlighting SHIPPED (plan/phase-12.md m2 — this is the SEAM, not a
//     highlighter): a code block gets the md_code_block role and its info string as a
//     label unless a host registers RenderOptions::highlight. Unregistered (the
//     default), every code line renders through the exact pre-seam code path, byte for
//     byte — the control in markdown_test.cpp.
//   - A highlighter returns SPANS (byte range in one code line -> Role), never a
//     painter and never text: the renderer stays in sole control of wrapping and the
//     cell grid. A span that overlaps a prior one, runs backwards, or exceeds the
//     line is clamped (or, if nothing of it survives, dropped) and named in
//     Rendered::highlight_report — never silently.
//   - A LONG code block folds to one summary line, and an unfolded one that is still
//     long is capped with the "▼ N more" marker (milestone 5b). Both hide LINES and
//     never TEXT: see CodeFoldOptions.
//
// LOGICAL TEXT (milestone 9, for selection): render_text() also returns the document's
// logical text — what the rendered lines would be at infinite width — and every drawn
// grapheme records the byte offset it came from in that text (or kNoSource for chrome:
// borders, rules, continuation-line indentation, the blank lines between blocks). A
// selection is therefore two offsets into this text and survives any re-wrap, and a
// copy yields text with no wrap artefacts. What counts as text, stated once:
//   - a paragraph or heading is one logical line (its inline text, heading marks
//     included, soft breaks as spaces) ending in "\n";
//   - a list marker, task box or quote bar on a block's FIRST line is text (a copied
//     list keeps its bullets and its indentation); on continuation lines it is chrome;
//   - a code block's lines are text, its box and label are chrome;
//   - a table row is its cells joined by "\t" and ended by "\n"; borders are chrome;
//   - a thematic break and a blank line between blocks each contribute "\n";
//   - a link's text and its appended " (url)" are both text (they are both drawn).
// The trailing "\n" of the last block is trimmed.
//
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Marker.hpp"
#include "rolltui/Style.hpp"

namespace rolltui::markdown {

// ---- the block tree ----------------------------------------------------------------

enum InlineStyle : unsigned {
  kPlain = 0,
  kEmphasis = 1u << 0,
  kStrong = 1u << 1,
  kCode = 1u << 2,
  kLink = 1u << 3,
  kStrike = 1u << 4,
  kLinkUrl = 1u << 5,  // the "(url)" the renderer appends after a link's text
  kUnderline = 1u << 6,
};

struct Run {
  std::string text;
  unsigned style = kPlain;
  std::string href;  // for kLink runs
};

enum class Align : std::uint8_t { Default, Left, Center, Right };

struct Block {
  enum class Kind : std::uint8_t { Paragraph, Heading, Code, Html, Quote, List, Item, Table, Rule };
  Kind kind = Kind::Paragraph;
  std::vector<Run> inlines;  // Paragraph, Heading
  int level = 0;             // Heading: 1-6
  std::string code;          // Code / Html: verbatim, '\n'-separated lines
  std::string info;          // Code: the fence's info string ("cpp")
  bool ordered = false;      // List
  unsigned start = 1;        // List (ordered)
  bool tight = true;         // List
  bool task = false;         // Item
  bool checked = false;      // Item
  std::vector<Block> children;  // Quote, List (Items), Item (blocks)
  // Table
  std::vector<Align> aligns;
  unsigned head_rows = 0;
  std::vector<std::vector<std::vector<Run>>> rows;  // rows × cells × runs
  bool implicit = false;  // a Paragraph the parser synthesised for bare text in a container
};

struct Document {
  std::vector<Block> blocks;
};

// Decodes "&amp;", "&#65;", "&#x42;" and the handful of named entities model output
// actually uses. An unknown named entity is returned verbatim (never dropped).
std::string decode_entity(std::string_view entity);

// The md4c flag set parse() uses, exposed so a test can collect md4c's own text
// callbacks under identical parsing and compare them with what was rendered.
unsigned parser_flags();

Document parse(std::string_view source);

// ---- rendering ---------------------------------------------------------------------

inline constexpr std::uint32_t kNoSource = 0xFFFFFFFFu;

struct Span {
  std::string text;
  int width = 0;
  Role role = Role::text;
  std::string href;                    // non-empty: hyperlink target for these cells (OSC 8)
  // One entry per grapheme cluster of `text` (as unicode::graphemes clusters it): the
  // byte offset of that grapheme in Rendered::text, or kNoSource for chrome. A span's
  // graphemes need not be contiguous in the logical text (a tab is eight spaces that
  // all point at the tab). Spans merge only when role and href both match.
  std::vector<std::uint32_t> sources;
};

struct StyledLine {
  std::vector<Span> spans;
  int width = 0;
};

// ---- syntax highlighting seam (plan/phase-12.md m2) ---------------------------------
//
// A code block's line renders in one role by default (md_code_block). A host that
// wants colour INSIDE the block registers RenderOptions::highlight: given the fence's
// language tag and one verbatim line of the block, it returns the byte ranges of that
// line that should take a different role. It returns DATA, never a painter — the
// renderer alone decides how those bytes wrap and land in the cell grid, so a
// highlighter that lies about its ranges is clamped, never able to corrupt a frame.
// The library ships no highlighter (plan/phase-12.md, "Deliberately NOT in this
// phase"): RenderOptions::highlight is unset by default, and an unset highlighter is
// never invoked — this is the whole mechanism by which a host that has decided its
// theme cannot show spans (a mono theme, most directly) asks for none: it simply does
// not register one.
struct HighlightSpan {
  std::size_t begin = 0;  // byte offset into the LINE passed to the callback
  std::size_t end = 0;    // exclusive
  Role role = Role::md_code_block;
};

// lang: the fence's info string, first whitespace-delimited word only ("cpp" from
// "cpp title=x.cpp"), or empty for an indented code block or a fence with no info
// string. lines: the block's verbatim lines, no trailing '\n' on any of them. index:
// which of them this call is about; the spans returned are byte offsets into
// lines[index] and nothing else. Called once per line of every Code block (fenced or
// indented); never for an HTML block (Markdown.hpp: HTML always renders as opaque code,
// never interpreted, so there is no language to highlight it by).
//
// WIDENED IN MILESTONE 5b, FROM `(lang, line)`, and the reason is worth keeping. m2
// made this contract deliberately minimal — one line, no context — and m5 immediately
// found the floor: word-level colouring inside a changed diff PAIR needs the line's
// NEIGHBOURS, and the only thing a one-line callback could reach (by remembering the
// previous line in its own state) is the `+` side, which colours one half of a pair and
// reads as a rendering bug. The third option — leave word-level out — was declined
// because the same wall is directly ahead for any real syntax highlighter: a string
// literal or a block comment that spans lines cannot be classified from the line alone.
// The block is already in the renderer's hand, so passing it costs nothing; what it
// buys is that a highlighter can be a pure function of the WHOLE block, which is still
// data-in/data-out and still clamped. What did NOT widen: the return type. A
// highlighter answers about lines[index] only, so the renderer keeps one line's spans
// to clamp against one line's length, and a highlighter that lies still cannot corrupt
// a frame.
using Highlighter =
    std::function<std::vector<HighlightSpan>(std::string_view lang, std::span<const std::string> lines, std::size_t index)>;

// What the renderer did with a highlighter's spans beyond drawing the well-formed
// ones: one entry per span it had to clamp or drop, naming the language, the line, the
// offending span and the correction — never silent, the way LayoutLoadReport and
// WindowsReport name their bad values.
struct HighlightReport {
  std::vector<std::string> clamped;
  bool clean() const { return clamped.empty(); }
};

// ---- long code blocks: fold and cap (plan/phase-12.md m5b) ---------------------------
//
// A model's answer routinely carries a 400-line block. Two thresholds, both off by
// default so nothing changes for a host that does not ask:
//
//   fold_over_lines  a block with MORE lines than this arrives FOLDED: one header line,
//                    "▸ diff · 42 lines · 1.2 kB", and no body.
//   cap_lines        an UNFOLDED block longer than this draws its first cap_lines and
//                    then one "▼ N more" row — the marker from Marker.hpp, so the
//                    transcript's and a block's say the same thing the same way.
//
// THE ONE RULE THAT MAKES THIS SAFE: **both hide LINES, never TEXT.** A folded or capped
// block still contributes every byte of its code to Rendered::text, and its header and
// marker rows are chrome (kNoSource). So the logical text — what a selection copies and
// what a find searches — is INDEPENDENT of fold state, and a match's offset cannot shift
// under it when a block opens or closes. The alternative (the folded block contributing
// its summary instead) shifts every offset after it in the entry, which is a highlight
// silently drawn over the wrong bytes: exactly the failure this project keeps finding.
// It costs one thing, stated rather than discovered: revealing a match inside a folded
// block has to OPEN it, which is what CodeBlockInfo::text_begin/end are for.
//
// Which block is which: `index` counts Code and Html blocks in document order, from 0.
// It is a property of the DOCUMENT, never of the render — a table too wide to fit is
// rendered as code but is NOT numbered, precisely because that would make a block's
// identity depend on the width and a user's fold toggle move to another block on resize.
struct CodeFoldState {
  std::size_t index = 0;
  bool folded = false;    // overrides fold_over_lines, in both directions
  bool uncapped = false;  // this block ignores cap_lines
};

struct CodeFoldOptions {
  int fold_over_lines = 0;  // 0: no block ever arrives folded
  int cap_lines = 0;        // 0: an unfolded block is never capped
  std::vector<CodeFoldState> states;  // the host's per-block overrides
  bool empty() const { return fold_over_lines <= 0 && cap_lines <= 0 && states.empty(); }
};

inline constexpr std::size_t kNoLine = static_cast<std::size_t>(-1);

// What the renderer did with one numbered code block — enough for a host to hit-test a
// click and to find the block holding a byte offset, without re-parsing anything.
struct CodeBlockInfo {
  std::size_t index = 0;
  std::string lang;        // the fence's first word; "" for a bare fence or indented block
  std::size_t lines = 0;   // the block's own line count
  std::size_t bytes = 0;
  bool foldable = false;   // over the threshold (or explicitly folded): it has a header row
  bool folded = false;
  std::size_t hidden = 0;  // lines the cap hides; 0 when not capped
  std::size_t header_line = kNoLine;  // index into Rendered::lines — the fold's click target
  std::size_t marker_line = kNoLine;  // …and the cap's
  std::size_t text_begin = 0, text_end = 0;  // the block's byte range in Rendered::text
};

// "diff · 42 lines · 1.2 kB" — the header's text, without its ▸/▾ marker. A bare fence
// has no language to name, so it is called "code".
std::string code_block_summary(std::string_view lang, std::size_t lines, std::size_t bytes);

struct RenderOptions {
  int width = 80;
  bool ambiguous_wide = false;
  int tab_width = 8;
  Role base = Role::text;
  Highlighter highlight;  // unset by default — see the seam comment above
  CodeFoldOptions code_fold;  // off by default — see above
};

struct Rendered {
  std::vector<StyledLine> lines;
  std::string text;  // the logical text every Span::sources offset indexes
  HighlightReport highlight_report;
  std::vector<CodeBlockInfo> code_blocks;  // one per numbered code block, in order
};

Rendered render_text(const Document& doc, const RenderOptions& opt = {});
Rendered render_text(std::string_view source, const RenderOptions& opt = {});
std::vector<StyledLine> render(const Document& doc, const RenderOptions& opt = {});
std::vector<StyledLine> render(std::string_view source, const RenderOptions& opt = {});

// The drawn text of the rendered lines, '\n'-joined — what a copy of the whole entry
// would yield, and what the never-drops-text test compares.
std::string plain_text(const std::vector<StyledLine>& lines);

}  // namespace rolltui::markdown
