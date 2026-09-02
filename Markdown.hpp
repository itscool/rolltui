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
//   - No syntax highlighting (plan/phase-9.md nice-to-have 9); a code block gets the
//     md_code_block role and its info string as a label.
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
#include <string>
#include <string_view>
#include <vector>

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

struct RenderOptions {
  int width = 80;
  bool ambiguous_wide = false;
  int tab_width = 8;
  Role base = Role::text;
};

struct Rendered {
  std::vector<StyledLine> lines;
  std::string text;  // the logical text every Span::sources offset indexes
};

Rendered render_text(const Document& doc, const RenderOptions& opt = {});
Rendered render_text(std::string_view source, const RenderOptions& opt = {});
std::vector<StyledLine> render(const Document& doc, const RenderOptions& opt = {});
std::vector<StyledLine> render(std::string_view source, const RenderOptions& opt = {});

// The drawn text of the rendered lines, '\n'-joined — what a copy of the whole entry
// would yield, and what the never-drops-text test compares.
std::string plain_text(const std::vector<StyledLine>& lines);

}  // namespace rolltui::markdown
