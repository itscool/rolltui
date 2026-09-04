#pragma once
//
// rolltui/Markdown.hpp — CommonMark + GFM (tables, strikethrough, task lists,
// autolinks) parsed by vendored md4c into OUR block tree, then rendered into styled,
// wrapped lines. md4c is an implementation detail of Markdown.cpp; nothing outside
// that file sees an MD_ type.
//
//   Document parse(std::string_view source)
//   Rendered r; r.render(doc | source, RenderOptions{})   — the caller owns the storage
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
//     Rendered::clamped() — never silently.
//   - A LONG code block folds to one summary line, and an unfolded one that is still
//     long is capped with the "▼ N more" marker (milestone 5b). Both hide LINES and
//     never TEXT: see CodeFoldOptions.
//
// LOGICAL TEXT (milestone 9, for selection): Rendered::text() is the document's
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
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Marker.hpp"
#include "rolltui/Style.hpp"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_md_lines.h"

namespace rolltui::markdown {

// THE STYLING VOCABULARY AS THE BOUNDARY CARRIES IT, exposed because the TRANSCRIPT renders
// entries through `rolltui_md_render` directly (Phase 15 m5e). One table of these bytes in
// the library, and the transcript is handed a POINTER to it rather than a second copy.
const RolltuiMdRoles* md_roles();

// ---- the parsed document -------------------------------------------------------------
//
// PHASE 15 m4 — THE BLOCK TREE LEFT THIS HEADER, and that is the port's one real
// SIMPLIFICATION rather than a cost. `Run`, `Block`, `Align` and the inline-style bits sat
// here for four phases and NOTHING outside the renderer ever read one; the two
// implementations now shape the tree the way their language wants (a nested
// `std::vector<Block>` with raw pointers into it on one side, index arrays with a
// first-child/next-sibling list on the other), and what a caller can ask is what the one
// test that cared was really asserting: what the top-level blocks ARE.

enum class BlockKind : std::uint8_t { Paragraph, Heading, Code, Html, Quote, List, Item, Table, Rule };

// A parsed document — an OWNED handle. Move-only: two owners of one tree is a lifetime
// question, and every lifetime in this library is structural.
//
// **PARSE INTO THE SAME OBJECT** when you parse repeatedly. A `Document` that is parsed
// into reuses every buffer it already had, which is what makes the transcript's parse
// cache — keyed on (id, version) and NOT on width — cost nothing after the first frame.
class Document {
 public:
  Document() = default;
  Document(Document&&) noexcept = default;
  Document& operator=(Document&&) noexcept = default;
  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;

  // OWNED, through a unique_ptr with a deleter that calls the C free — one owner, and no
  // hand-rolled `delete` anywhere. Made on FIRST USE, so a default-constructed Document
  // holds nothing and costs nothing.
  struct Handle {
    void operator()(RolltuiMdDoc* p) const { rolltui_md_doc_free(p); }
  };

  void parse(std::string_view source);

  std::size_t block_count() const { return doc_ ? rolltui_md_doc_block_count(doc_.get()) : 0; }
  BlockKind block_kind(std::size_t i) const {
    return static_cast<BlockKind>(rolltui_md_doc_block_kind(doc_.get(), i));
  }
  // A Code or Html block's verbatim text; empty for every other kind. A BORROW, valid
  // until this document is parsed into again.
  std::string_view block_code(std::size_t i) const {
    std::size_t n = 0;
    const char* p = rolltui_md_doc_block_code(doc_.get(), i, &n);
    return {p, n};
  }

  const RolltuiMdDoc* handle() const { return doc_.get(); }

 private:
  std::unique_ptr<RolltuiMdDoc, Handle> doc_;
};

// A fresh Document holding this source. The handle moves; nothing is copied.
Document parse(std::string_view source);

// Decodes "&amp;", "&#65;", "&#x42;" and the handful of named entities model output
// actually uses. An unknown named entity is returned verbatim (never dropped).
std::string decode_entity(std::string_view entity);

// The md4c flag set parse() uses, exposed so a test can collect md4c's own text
// callbacks under identical parsing and compare them with what was rendered.
unsigned parser_flags();

// ---- rendering ---------------------------------------------------------------------
//
// PHASE 15 m4 — A SPAN OWNS NOTHING, AND THE RENDER IS A HANDLE THE CALLER REUSES.
// `Span` and `StyledLine` ARE the C structs (`rolltui/c/rolltui_md_lines.h`, one
// definition, the rule Phase 14 fixed), and every string and array in them is a BORROW
// into the `Rendered` that produced them, valid until it is rendered into again or
// destroyed — the window `Line`, `Frame::glyph` and `Scratch` already state.
//
// WHAT A CALLER CAN SEE, both forced by the store rather than chosen:
//   - `s.text()`, `s.href()` and `s.sources()` are views, not members. A caller that
//     wants to keep the bytes says so.
//   - `render_text` fills a `Rendered` the CALLER owns rather than returning containers.
//     A `Rendered` that is rendered into repeatedly grows to a high-water mark and never
//     allocates again, which is what makes a re-laid transcript entry free.
//
// WHY (m1's measurement, and it is the whole reason this shape changed): a resize frame
// spent **4,128 allocations in `push_span`** and **2,283 more copying one span into
// another line**, every frame, for bytes that had not changed. Nobody decided a span
// should own its text; `std::string` is what you type.

using Span = RolltuiMdSpan;
using StyledLine = RolltuiMdLine;

inline constexpr std::uint32_t kNoSource = ROLLTUI_MD_NO_SOURCE;

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
// PHASE 15 m4 — `lines` IS A SPAN OF VIEWS, and the port forced it: nothing crosses the
// boundary as a `std::string`, so the C hands over (pointer, length) pairs, and materialising
// a `std::string` per line to satisfy the old signature would have been an allocation per
// highlighted line for the sake of a type. Every highlighter reads its lines and copies none
// of them, so the views cost nothing and say so.
using Highlighter = std::function<std::vector<HighlightSpan>(std::string_view lang,
                                                             std::span<const std::string_view> lines,
                                                             std::size_t index)>;

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
// block still contributes every byte of its code to Rendered::text(), and its header and
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

inline constexpr std::size_t kNoLine = ROLLTUI_MD_NO_LINE;

// What the renderer did with one numbered code block — enough for a host to hit-test a
// click and to find the block holding a byte offset, without re-parsing anything.
// `lang` is a BORROW like every other string here (`b.lang()`).
using CodeBlockInfo = RolltuiMdCodeBlock;

// "diff · 42 lines · 1.2 kB" — the header's text, without its ▸/▾ marker. A bare fence
// has no language to name, so it is called "code". FILLS A CALLER'S BUFFER and returns the
// bytes written (`kSummaryMax` is enough for any of it): this is a per-frame path when a
// document holds foldable blocks, so it may not hand back a `std::string`.
inline constexpr std::size_t kSummaryMax = ROLLTUI_MD_SUMMARY_MAX;
std::size_t code_block_summary(std::string_view lang, std::size_t lines, std::size_t bytes, char* out,
                               std::size_t cap);

struct RenderOptions {
  int width = 80;
  bool ambiguous_wide = false;
  int tab_width = 8;
  Role base = Role::text;
  Highlighter highlight;  // unset by default — see the seam comment above
  CodeFoldOptions code_fold;  // off by default — see above
};

// WHAT A RENDER PRODUCED, and the storage it produced it in. OWNED (CLAUDE.md's fourth
// strategy) through one handle; move-only, because two owners of one span pool is a
// lifetime question and every lifetime in this library is structural.
//
// **RE-RENDER INTO THE SAME OBJECT.** `render()` resets and refills every buffer, so an
// entry that is re-laid at a new width reuses the bytes, the spans, the sources and the
// logical text it already had. That is the milestone: the store is what turned m1's
// 6,411 span allocations into pool growth that stops.
class Rendered {
 public:
  Rendered() = default;
  Rendered(Rendered&&) noexcept = default;
  Rendered& operator=(Rendered&&) noexcept = default;
  Rendered(const Rendered&) = delete;
  Rendered& operator=(const Rendered&) = delete;

  // OWNED, through a unique_ptr with a deleter that calls the C free — one owner, and no
  // hand-rolled `delete` anywhere. Made on FIRST USE, so a default-constructed Rendered
  // (a member of a cache entry, a Scratch's fresh T) holds nothing and costs nothing.
  struct Handle {
    void operator()(RolltuiMdLines* p) const { rolltui_md_lines_free(p); }
  };

  void render(const Document& doc, const RenderOptions& opt = {});
  void render(std::string_view source, const RenderOptions& opt = {});

  std::span<const StyledLine> lines() const {
    const RolltuiMdLine* p = store_ ? rolltui_md_lines_all(store_.get()) : nullptr;
    return {p, p ? rolltui_md_lines_count(store_.get()) : 0};
  }
  std::size_t line_count() const { return store_ ? rolltui_md_lines_count(store_.get()) : 0; }
  const StyledLine& line(std::size_t i) const { return *rolltui_md_lines_line(store_.get(), i); }
  // The document's logical text — what every Span::sources() offset indexes.
  std::string_view text() const {
    return store_ ? std::string_view(rolltui_md_lines_text(store_.get()), rolltui_md_lines_text_size(store_.get()))
                  : std::string_view{};
  }
  std::span<const CodeBlockInfo> code_blocks() const {
    const RolltuiMdCodeBlock* p = store_ ? rolltui_md_lines_code_blocks(store_.get()) : nullptr;
    return {p, p ? rolltui_md_lines_code_block_count(store_.get()) : 0};
  }
  // Every highlight span the renderer had to clamp or drop, named by language and line —
  // never silent, the way LayoutLoadReport and WindowsReport name their bad values.
  std::size_t clamped_count() const { return store_ ? rolltui_md_lines_clamped_count(store_.get()) : 0; }
  std::string_view clamped(std::size_t i) const {
    const char* p = nullptr;
    std::size_t n = 0;
    rolltui_md_lines_clamped_at(store_.get(), i, &p, &n);
    return {p, n};
  }
  bool highlight_clean() const { return clamped_count() == 0; }

  // The store, for a caller that BUILDS lines of its own behind the rendered ones —
  // the transcript putting an entry's prefix in front of every body line without
  // copying a byte (`rolltui_md_lines_span_ref`). Nothing else needs it.
  RolltuiMdLines* store() {
    if (!store_) store_.reset(rolltui_md_lines_new());
    return store_.get();
  }
  const RolltuiMdLines* store() const { return store_.get(); }

 private:
  std::unique_ptr<RolltuiMdLines, Handle> store_;
  // The syntax-highlighting seam's own working memory: a block's lines as views, and the
  // spans a highlighter hands back. Members rather than locals in `render()`, which runs
  // per entry per frame — a local vector would allocate on every block it highlighted.
  std::vector<std::string_view> hl_lines_;
  std::vector<HighlightSpan> hl_spans_;
};

// A fresh `Rendered` holding this document at this width. The handle moves; nothing is
// copied. A caller that renders repeatedly (the transcript's layout cache) keeps its own
// `Rendered` and calls `render()` on it instead, which is what makes the second render
// free.
Rendered render_text(const Document& doc, const RenderOptions& opt = {});
Rendered render_text(std::string_view source, const RenderOptions& opt = {});

// The drawn text of `lines`, '\n'-joined — what a copy of the whole entry would yield,
// and what the never-drops-text test compares. `plain_text_into` fills a caller's string
// (cleared first); the two returning forms are for callers that are not on a frame path —
// today that is the test suite, and the rule (CLAUDE.md) is about per-frame APIs.
void plain_text_into(std::span<const StyledLine> lines, std::string& out);
std::string plain_text(std::span<const StyledLine> lines);
std::string plain_text(const StyledLine& line);

}  // namespace rolltui::markdown

