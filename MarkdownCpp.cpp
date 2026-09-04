// rolltui/MarkdownCpp.cpp — the C++ side of the markdown renderer, behind the same
// boundary as `c/rolltui_markdown.c` (rolltui/c/rolltui_markdown.h, Phase 15 m4). One CMake
// flag picks which of the two links; both satisfy the ~160 assertions in
// `rolltui/tests/markdown_test.cpp` and every golden frame, so a behavioural difference is
// a test failure on the day it appears rather than a review comment.
//
// This is the code Phase 9 m4 wrote and Phase 13 m5b thinned, moved behind the boundary and
// given the store's buffers to fill. It is deliberately NOT a transliteration of the C: the
// comparison m4 is here to make is between two languages writing the same design naturally,
// not between one language and the other's shadow — which is why the block tree here is a
// nested `std::vector<Block>` with raw pointers into it while the C uses index arrays and a
// child list, and why neither shape appears in the public header any more.
//
// md4c (rolltui/third_party/md4c, MIT, pinned in its README) is included here and in the C
// implementation, and nowhere else.
#include "rolltui/c/rolltui_markdown.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"
#include "rolltui/c/rolltui_marker.h"
#include "rolltui/third_party/md4c/md4c.h"

namespace {

// The inline style bits and the block kinds, which used to be in the public header and are
// this implementation's own business now — the C's are its own, in its own spelling.
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

enum class Align : std::uint8_t { Default, Left, Center, Right };

struct Run {
  std::string text;
  unsigned style = kPlain;
  std::string href;  // for kLink runs
};

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
  std::vector<std::vector<std::vector<Run>>> rows;  // rows x cells x runs
  bool implicit = false;  // a Paragraph the parser synthesised for bare text in a container
};

std::string decode_entity(std::string_view ent);

using Span = RolltuiMdSpan;
using CodeBlockInfo = RolltuiMdCodeBlock;
using rolltui::Line;
using rolltui::WrapGrapheme;
using rolltui::WrapLines;
using rolltui::WrapOptions;
namespace unicode = rolltui::unicode;



// ---- entities ----------------------------------------------------------------------

std::string decode_entity(std::string_view ent) {
  struct Named { const char* name; const char* utf8; };
  static const Named kNamed[] = {
      {"amp", "&"},   {"lt", "<"},     {"gt", ">"},      {"quot", "\""},    {"apos", "'"},
      {"nbsp", "\xC2\xA0"}, {"copy", "\xC2\xA9"}, {"reg", "\xC2\xAE"}, {"trade", "\xE2\x84\xA2"},
      {"hellip", "\xE2\x80\xA6"}, {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"},
      {"laquo", "\xC2\xAB"}, {"raquo", "\xC2\xBB"}, {"ldquo", "\xE2\x80\x9C"}, {"rdquo", "\xE2\x80\x9D"},
      {"lsquo", "\xE2\x80\x98"}, {"rsquo", "\xE2\x80\x99"}, {"bull", "\xE2\x80\xA2"},
      {"rarr", "\xE2\x86\x92"}, {"larr", "\xE2\x86\x90"}, {"times", "\xC3\x97"}, {"deg", "\xC2\xB0"},
  };
  if (ent.size() < 3 || ent.front() != '&' || ent.back() != ';') return std::string(ent);
  std::string_view body = ent.substr(1, ent.size() - 2);
  if (!body.empty() && body[0] == '#') {
    std::string_view num = body.substr(1);
    int base = 10;
    if (!num.empty() && (num[0] == 'x' || num[0] == 'X')) { base = 16; num = num.substr(1); }
    if (num.empty()) return std::string(ent);
    unsigned long v = 0;
    for (char c : num) {
      int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else return std::string(ent);
      v = v * static_cast<unsigned long>(base) + static_cast<unsigned long>(d);
      if (v > 0x10FFFF) return "\xEF\xBF\xBD";
    }
    if (v == 0 || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
    std::string out;
    unicode::append_utf8(out, static_cast<char32_t>(v));
    return out;
  }
  for (const Named& n : kNamed)
    if (body == n.name) return n.utf8;
  return std::string(ent);
}

unsigned parser_flags() {
  return MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |
         MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_PERMISSIVEWWWAUTOLINKS;
}

// ---- parsing -----------------------------------------------------------------------

struct ParseState {
  std::vector<Block>* blocks = nullptr;
  std::vector<Block*> stack;        // open blocks; stack.back() receives text
  std::vector<unsigned> spans;      // open inline styles, one bit each
  std::vector<std::string> hrefs;   // open links' hrefs
  std::vector<bool> autolinks;
  std::vector<std::vector<Run>>* cur_row = nullptr;
  std::vector<Run>* cur_cell = nullptr;

  static std::string attr(const MD_ATTRIBUTE& a) {
    std::string s;
    for (unsigned i = 0; a.substr_offsets && a.substr_offsets[i] < a.size; ++i) {
      std::string_view piece(a.text + a.substr_offsets[i], a.substr_offsets[i + 1] - a.substr_offsets[i]);
      if (a.substr_types[i] == MD_TEXT_ENTITY) s += decode_entity(piece);
      else if (a.substr_types[i] == MD_TEXT_NULLCHAR) s += "\xEF\xBF\xBD";
      else s += piece;
    }
    return s;
  }

  unsigned style() const {
    unsigned s = kPlain;
    for (unsigned b : spans) s |= b;
    return s;
  }
  Block& top() { return *stack.back(); }
  static bool is_container(const Block& b) {
    return b.kind == Block::Kind::Quote || b.kind == Block::Kind::Item || b.kind == Block::Kind::List;
  }
  void pop_implicit() {
    if (!stack.empty() && top().implicit) stack.pop_back();
  }
  // Pointers on the stack stay valid because only the top block's `children` ever
  // grows, and a block never moves while it is on the stack (its parent's vector is
  // untouched until the block is popped).
  Block& push_child(Block b) {
    Block* parent = stack.empty() ? nullptr : stack.back();
    std::vector<Block>& into = parent ? parent->children : *blocks;
    into.push_back(std::move(b));
    stack.push_back(&into.back());
    return into.back();
  }
  // Where text goes: a table cell, the open paragraph/heading, or a synthesised
  // paragraph when md4c hands text straight to a container (tight list items).
  void append_run(std::string text, unsigned style_bits, const std::string& href = "") {
    if (text.empty()) return;
    std::vector<Run>* runs;
    if (cur_cell) runs = cur_cell;
    else {
      Block* b = stack.empty() ? nullptr : stack.back();
      if (!b || is_container(*b)) {
        Block p;
        p.kind = Block::Kind::Paragraph;
        p.implicit = true;
        b = &push_child(std::move(p));
      }
      runs = &b->inlines;
    }
    if (!runs->empty() && runs->back().style == style_bits && runs->back().href == href) {
      runs->back().text += text;
      return;
    }
    runs->push_back({std::move(text), style_bits, href});
  }
};

int enter_block(MD_BLOCKTYPE type, void* det, void* ud) {
  ParseState& st = *static_cast<ParseState*>(ud);
  st.pop_implicit();
  Block b;
  switch (type) {
    case MD_BLOCK_DOC: return 0;
    case MD_BLOCK_QUOTE: b.kind = Block::Kind::Quote; break;
    case MD_BLOCK_UL: {
      auto* d = static_cast<MD_BLOCK_UL_DETAIL*>(det);
      b.kind = Block::Kind::List;
      b.ordered = false;
      b.tight = d->is_tight != 0;
      break;
    }
    case MD_BLOCK_OL: {
      auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(det);
      b.kind = Block::Kind::List;
      b.ordered = true;
      b.start = d->start;
      b.tight = d->is_tight != 0;
      break;
    }
    case MD_BLOCK_LI: {
      auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(det);
      b.kind = Block::Kind::Item;
      b.task = d->is_task != 0;
      b.checked = b.task && (d->task_mark == 'x' || d->task_mark == 'X');
      break;
    }
    case MD_BLOCK_HR: b.kind = Block::Kind::Rule; break;
    case MD_BLOCK_H:
      b.kind = Block::Kind::Heading;
      b.level = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(det)->level);
      break;
    case MD_BLOCK_CODE: {
      auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(det);
      b.kind = Block::Kind::Code;
      b.info = ParseState::attr(d->info);
      break;
    }
    case MD_BLOCK_HTML: b.kind = Block::Kind::Html; break;
    case MD_BLOCK_P: b.kind = Block::Kind::Paragraph; break;
    case MD_BLOCK_TABLE: {
      auto* d = static_cast<MD_BLOCK_TABLE_DETAIL*>(det);
      b.kind = Block::Kind::Table;
      b.head_rows = d->head_row_count;
      b.aligns.assign(d->col_count, Align::Default);
      break;
    }
    case MD_BLOCK_THEAD: case MD_BLOCK_TBODY: return 0;
    case MD_BLOCK_TR: {
      Block& t = st.top();
      t.rows.emplace_back();
      st.cur_row = &t.rows.back();
      return 0;
    }
    case MD_BLOCK_TH: case MD_BLOCK_TD: {
      auto* d = static_cast<MD_BLOCK_TD_DETAIL*>(det);
      Block& t = st.top();
      if (!st.cur_row) { t.rows.emplace_back(); st.cur_row = &t.rows.back(); }
      st.cur_row->emplace_back();
      st.cur_cell = &st.cur_row->back();
      std::size_t col = st.cur_row->size() - 1;
      if (type == MD_BLOCK_TH && col < t.aligns.size()) {
        switch (d->align) {
          case MD_ALIGN_LEFT: t.aligns[col] = Align::Left; break;
          case MD_ALIGN_CENTER: t.aligns[col] = Align::Center; break;
          case MD_ALIGN_RIGHT: t.aligns[col] = Align::Right; break;
          default: break;
        }
      }
      return 0;
    }
    default:
      // A block kind this renderer does not know (footnotes, admonitions — not
      // enabled) — treated as a quote so its text is never dropped.
      b.kind = Block::Kind::Quote;
      break;
  }
  st.push_child(std::move(b));
  return 0;
}

int leave_block(MD_BLOCKTYPE type, void*, void* ud) {
  ParseState& st = *static_cast<ParseState*>(ud);
  switch (type) {
    case MD_BLOCK_DOC: st.pop_implicit(); return 0;
    case MD_BLOCK_THEAD: case MD_BLOCK_TBODY: return 0;
    case MD_BLOCK_TR: st.cur_row = nullptr; return 0;
    case MD_BLOCK_TH: case MD_BLOCK_TD: st.cur_cell = nullptr; return 0;
    default: break;
  }
  st.pop_implicit();
  if (!st.stack.empty()) st.stack.pop_back();
  return 0;
}

int enter_span(MD_SPANTYPE type, void* det, void* ud) {
  ParseState& st = *static_cast<ParseState*>(ud);
  switch (type) {
    case MD_SPAN_EM: st.spans.push_back(kEmphasis); break;
    case MD_SPAN_STRONG: st.spans.push_back(kStrong); break;
    case MD_SPAN_CODE: st.spans.push_back(kCode); break;
    case MD_SPAN_DEL: st.spans.push_back(kStrike); break;
    case MD_SPAN_U: st.spans.push_back(kUnderline); break;
    case MD_SPAN_A: {
      auto* d = static_cast<MD_SPAN_A_DETAIL*>(det);
      st.spans.push_back(kLink);
      st.hrefs.push_back(ParseState::attr(d->href));
      st.autolinks.push_back(d->is_autolink != 0);
      break;
    }
    case MD_SPAN_IMG: {
      auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(det);
      st.spans.push_back(kLink);
      st.hrefs.push_back(ParseState::attr(d->src));
      st.autolinks.push_back(false);
      break;
    }
    default: st.spans.push_back(kPlain); break;  // math, wikilinks, …: text passes through
  }
  return 0;
}

int leave_span(MD_SPANTYPE type, void*, void* ud) {
  ParseState& st = *static_cast<ParseState*>(ud);
  if (type == MD_SPAN_A || type == MD_SPAN_IMG) {
    std::string href = st.hrefs.empty() ? "" : st.hrefs.back();
    bool autolink = st.autolinks.empty() ? false : st.autolinks.back();
    if (!st.hrefs.empty()) st.hrefs.pop_back();
    if (!st.autolinks.empty()) st.autolinks.pop_back();
    if (!st.spans.empty()) st.spans.pop_back();
    if (!autolink && !href.empty()) st.append_run(" (" + href + ")", st.style() | kLinkUrl);
    return 0;
  }
  if (!st.spans.empty()) st.spans.pop_back();
  return 0;
}

int text(MD_TEXTTYPE type, const MD_CHAR* txt, MD_SIZE size, void* ud) {
  ParseState& st = *static_cast<ParseState*>(ud);
  std::string_view s(txt, size);
  Block* b = st.stack.empty() ? nullptr : st.stack.back();
  bool verbatim = b && !st.cur_cell && (b->kind == Block::Kind::Code || b->kind == Block::Kind::Html);
  if (verbatim) {
    if (type == MD_TEXT_NULLCHAR) b->code += "\xEF\xBF\xBD";
    else b->code.append(s);
    return 0;
  }
  const std::string href = st.hrefs.empty() ? std::string() : st.hrefs.back();
  switch (type) {
    case MD_TEXT_NULLCHAR: st.append_run("\xEF\xBF\xBD", st.style(), href); break;
    case MD_TEXT_BR: st.append_run("\n", st.style(), href); break;
    case MD_TEXT_SOFTBR: st.append_run(" ", st.style(), href); break;
    case MD_TEXT_ENTITY: st.append_run(decode_entity(s), st.style(), href); break;
    case MD_TEXT_CODE: st.append_run(std::string(s), st.style() | kCode, href); break;
    default: st.append_run(std::string(s), st.style(), href); break;  // NORMAL, HTML, LATEXMATH
  }
  return 0;
}


// ---- rendering ---------------------------------------------------------------------
//
// PHASE 15 m4 — EVERY LINE, SPAN AND BYTE THIS PRODUCES LIVES IN THE CALLER'S STORE
// (`rolltui/c/rolltui_md_lines.h`). Nothing below allocates a per-span buffer, and the
// only owning containers left are the WORKING memory in `Work`, which is one per thread
// and reused forever. m1 measured what this replaced: 4,128 allocations a resize frame in
// `push_span` alone, plus 692 in `layout_runs`, 638 in `render_code`, 320 in
// `render_blocks` and 240 in `Ctx::child` — every one of them a container nobody decided
// to own, rebuilt every frame to hold bytes that had not changed.

unsigned char role_for(const RolltuiMdRoles& roles, unsigned style, unsigned char base) {
  if (style & kCode) return roles.code_inline;
  if (style & kLinkUrl) return roles.link_url;
  if (style & kLink) return roles.link;
  if (style & kStrike) return roles.strikethrough;
  if (style & kStrong) return roles.strong;
  if (style & kEmphasis) return roles.emphasis;
  if (style & kUnderline) return roles.link;
  return base;
}

// One piece of a block's line prefix — a quote bar, a list marker, the padding under it.
// The BYTES are interned in the store (its own pool, so they cannot disturb span merging)
// and this is the (offset, length, role) that names them. It used to be a `Span` holding a
// `std::string`, copied into a fresh `std::vector<Span>` by every `Ctx::child`.
struct PrefixPart {
  std::size_t off = 0, len = 0;
  unsigned char role = 0;
};

// Where one run's text starts in the concatenated paragraph, and how it draws.
struct RunAt {
  std::size_t start;
  unsigned char role;
  const std::string* href;
};

// One clamped, non-overlapping, in-line highlight range, in the byte-offset space of
// the code line the highlighter was given (WrapGrapheme::source_offset matches it).
struct HighlightRun {
  std::size_t begin, end;
  unsigned char role;
};

// THE RENDERER'S WORKING MEMORY, in one place, hanging off the STORE the caller owns
// (`rolltui_md_lines_work`; CLAUDE.md's third strategy: "this function needs somewhere to
// work" is not a new strategy, it is a missing handle). Per-store rather than per-thread so
// that a `Rendered` rendered into every frame reuses it along with everything else, and so
// that there is nothing retained for `shutdown()` to have to find. `busy` is the window,
// ENFORCED rather than promised: a highlighter that called back into this render would
// abort rather than silently share the buffers of the render it is inside.
struct Work {
  std::vector<PrefixPart> prefix;      // every Ctx's prefix is a RANGE into this
  std::vector<std::uint32_t> srcs;     // per-grapheme source offsets for one span
  std::string run_text;                // a paragraph's runs, concatenated
  std::vector<RunAt> run_at;
  std::vector<HighlightRun> hruns;
  std::vector<RolltuiMdHighlightSpan> hspans;
  std::vector<RolltuiMdCodeLine> block_lines;  // a code block's lines, ONLY when highlighting
  std::string scratch;                 // one string under construction (a rule, a message)
  std::string pad;                     // a run of spaces, grown to the widest ever needed
  std::string cell_text;               // a table cell's plain text, for its natural width
  std::vector<std::pair<std::size_t, std::size_t>> code_lines;  // (offset, length) into a block
  std::vector<Run> heading_runs;       // a heading's "## " marks in front of its own runs
  std::vector<int> col_width;
  std::vector<std::pair<std::size_t, std::size_t>> cell_at;  // (first aux line, count) per column
  WrapLines wrap;                      // the one wrap engine every call reuses
  bool busy = false;

  // Reset between renders. Every buffer here holds PLAIN DATA — the port made
  // `block_lines` a vector of (pointer, length) views rather than of `std::string`s — so
  // `clear()` is the right reset for all of them, which is exactly the question Phase 13
  // found four wrong answers to.
  void clear() {
    prefix.clear();
    srcs.clear();
    run_text.clear();
    run_at.clear();
    hruns.clear();
    hspans.clear();
    scratch.clear();
    code_lines.clear();
    heading_runs.clear();
    col_width.clear();
    cell_at.clear();
  }
};

// Everything one render needs that is not per-block. `out` owns the logical text, the
// interned chrome, the code blocks and the report; `lines` is where lines are being built
// RIGHT NOW, which is `out` except while a table's cells are laid out at their column's
// width before being poured into the row.
struct State {
  RolltuiMdLines* out = nullptr;
  RolltuiMdLines* lines = nullptr;
  Work* w = nullptr;
  const RolltuiMdRenderOptions* opt = nullptr;
  bool ambiguous = false;
  int tab_width = 8;
  std::size_t code_index = 0;

  const RolltuiMdRoles& roles() const { return opt->roles; }
};

std::string_view spaces(Work& w, int n) {
  const std::size_t need = static_cast<std::size_t>(n > 0 ? n : 0);
  if (w.pad.size() < need) w.pad.resize(need, ' ');
  return std::string_view(w.pad).substr(0, need);
}

// Consecutive logical offsets for `text` appended to the logical text at `base`, into the
// one buffer every span of every line reuses.
void sources_for(State& st, std::string_view text, std::size_t base) {
  std::size_t n = 0;
  const RolltuiUnicodeGrapheme* gs =
      rolltui_md_lines_clusters(st.lines, text.data(), text.size(), st.ambiguous, &n, nullptr);
  st.w->srcs.clear();
  for (std::size_t i = 0; i < n; ++i) st.w->srcs.push_back(static_cast<std::uint32_t>(base + gs[i].offset));
}

void span(State& st, std::string_view text, unsigned char role, std::span<const std::uint32_t> sources = {},
          std::string_view href = {}) {
  rolltui_md_lines_span(st.lines, text.data(), text.size(), role, st.ambiguous,
                        sources.empty() ? nullptr : sources.data(), sources.size(), href.data(), href.size());
}

// Rendering context: the width available for content, the prefix each line carries (a
// marker on the first line, indentation on the rest, both as RANGES into Work::prefix),
// and what ends this block's logical line.
struct Ctx {
  int width = 1;
  std::size_t pf_off = 0, pf_len = 0;  // the first line's prefix
  std::size_t pr_off = 0, pr_len = 0;  // every other line's
  bool first_used = false;
  unsigned char base = 0;
  const char* terminator = "\n";  // "\t" inside a table row: a row is ONE logical line

  void take_prefix(std::size_t& off, std::size_t& len) {
    if (!first_used) {
      first_used = true;
      off = pf_off;
      len = pf_len;
      return;
    }
    off = pr_off;
    len = pr_len;
  }
  // A child block's context: narrower, and carrying THIS block's prefix in front of its
  // own. The parent's first-line prefix is CONSUMED here (a nested block's first line is
  // the parent's first line), which is why this is not const.
  Ctx child(State& st, int less, std::span<const PrefixPart> first_extra,
            std::span<const PrefixPart> rest_extra, unsigned char new_base) {
    Ctx c = *this;
    c.width = width - less;
    if (c.width < 1) c.width = 1;
    std::size_t off = 0, len = 0;
    take_prefix(off, len);
    std::vector<PrefixPart>& p = st.w->prefix;
    // Reserved BEFORE the copy because the source range is in the same vector: a growth
    // mid-loop would move the elements being read.
    p.reserve(p.size() + len + first_extra.size() + pr_len + rest_extra.size());
    const std::size_t fo = p.size();
    for (std::size_t i = 0; i < len; ++i) p.push_back(p[off + i]);
    for (const PrefixPart& e : first_extra) p.push_back(e);
    const std::size_t ro = p.size();
    for (std::size_t i = 0; i < pr_len; ++i) p.push_back(p[pr_off + i]);
    for (const PrefixPart& e : rest_extra) p.push_back(e);
    c.pf_off = fo;
    c.pf_len = ro - fo;
    c.pr_off = ro;
    c.pr_len = p.size() - ro;
    c.first_used = false;
    c.base = new_base;
    return c;
  }
  void end_logical_line(State& st) const { rolltui_md_lines_text_append(st.out, terminator, std::strlen(terminator)); }
};

// Interns one prefix piece's bytes and names them.
PrefixPart part(State& st, std::string_view text, unsigned char role) {
  PrefixPart p;
  p.off = rolltui_md_lines_intern(st.out, text.data(), text.size());
  p.len = text.size();
  p.role = role;
  return p;
}

// Opens a line and lays down the context's prefix. On a block's first line the prefix is
// logical text (the marker is copied with the item); elsewhere it is chrome.
void start_line(State& st, Ctx& ctx, bool first_of_block = false) {
  rolltui_md_lines_open(st.lines);
  std::size_t off = 0, len = 0;
  ctx.take_prefix(off, len);
  for (std::size_t i = 0; i < len; ++i) {
    const PrefixPart p = st.w->prefix[off + i];
    // The chrome pool is not the span pool and not the text pool, so this stays valid
    // across both appends below.
    const std::string_view text(rolltui_md_lines_interned(st.out, p.off), p.len);
    if (first_of_block) {
      const std::size_t base = rolltui_md_lines_text_size(st.out);
      rolltui_md_lines_text_append(st.out, text.data(), text.size());
      sources_for(st, text, base);
      span(st, text, p.role, st.w->srcs);
    } else {
      span(st, text, p.role);
    }
  }
}

// A chrome-only first line (a code box's top rule, a table's top border) still carries
// the block's marker; the marker then stands on a logical line of its own.
void finish_chrome_first_line(State& st, Ctx& ctx, std::size_t logical_before) {
  if (rolltui_md_lines_text_size(st.out) > logical_before) ctx.end_logical_line(st);
}

void blank_line(State& st, Ctx& ctx) {
  start_line(st, ctx);
  // Trim trailing spaces so a blank line inside a list is empty and a blank line inside a
  // quote is just its bar.
  rolltui_md_lines_trim_trailing_spaces(st.lines, st.ambiguous);
  ctx.end_logical_line(st);
  rolltui_md_lines_close(st.lines);
}

// Lay out inline runs as wrapped lines with per-grapheme roles and hrefs. The runs'
// concatenated text becomes one logical line.
void layout_runs(State& st, const std::vector<Run>& runs, Ctx& ctx, int first_indent = 0,
                 int hanging_indent = 0, unsigned char role_override = ROLLTUI_MD_NO_ROLE) {
  Work& w = *st.w;
  w.run_text.clear();
  w.run_at.clear();
  for (const Run& r : runs) {
    const unsigned char role = role_override != ROLLTUI_MD_NO_ROLE ? role_override : role_for(st.roles(), r.style, ctx.base);
    w.run_at.push_back({w.run_text.size(), role, (r.style & kLink) ? &r.href : nullptr});
    w.run_text += r.text;
  }
  WrapOptions wo;
  wo.ambiguous_wide = st.ambiguous;
  wo.tab_width = st.tab_width;
  wo.first_indent = first_indent;
  wo.hanging_indent = hanging_indent;
  w.wrap.wrap(w.run_text, ctx.width, wo);
  auto run_at = [&](std::size_t off) -> const RunAt* {
    const RunAt* r = nullptr;
    for (const RunAt& a : w.run_at) {
      if (a.start <= off) r = &a;
      else break;
    }
    return r;
  };
  std::size_t base = 0;
  std::size_t k = 0;
  for (const Line& ln : w.wrap) {
    start_line(st, ctx, k == 0);
    if (k == 0) {
      base = rolltui_md_lines_text_size(st.out);
      rolltui_md_lines_text_append(st.out, w.run_text.data(), w.run_text.size());
    }
    if (ln.indent > 0) span(st, spaces(w, ln.indent), ctx.base);
    for (const WrapGrapheme& g : ln.graphemes) {
      const RunAt* r = run_at(g.source_offset);
      const std::uint32_t one = static_cast<std::uint32_t>(base + g.source_offset);
      span(st, ln.text.substr(g.offset, g.length), r ? r->role : ctx.base,
           std::span<const std::uint32_t>(&one, 1), (r && r->href) ? std::string_view(*r->href) : std::string_view{});
    }
    rolltui_md_lines_close(st.lines);
    ++k;
  }
  ctx.end_logical_line(st);
}

// A code block's lines as (offset, length) ranges into `code` — no copy, where the old
// `split_lines` built a whole `std::vector<std::string>` per block per frame.
void split_lines(Work& w, const std::string& code) {
  w.code_lines.clear();
  std::size_t start = 0;
  while (start <= code.size()) {
    std::size_t nl = code.find('\n', start);
    if (nl == std::string::npos) {
      if (start < code.size()) w.code_lines.push_back({start, code.size() - start});
      break;
    }
    w.code_lines.push_back({start, nl - start});
    start = nl + 1;
  }
}

// The first whitespace-delimited word of a fence's info string ("cpp" from
// "cpp title=x.cpp") — the language tag a highlighter is called with. CommonMark
// permits attributes after the language; the box label (rendered as-is, unchanged)
// keeps the whole string, only the highlighter's tag is narrowed.
std::string_view first_word(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
  return s.substr(0, i);
}

// Turns a highlighter's raw spans for ONE code line into a run list that is safe to
// paint: sorted by start (stable, so ties keep the highlighter's own order), every
// span forced forward and inside [0, line_len), later spans losing ground to earlier
// ones on overlap. Every span the input needed correcting or dropping — never merely
// "produced a run" — is named in the render's report, by language and 1-based line
// number, the way LayoutLoadReport/WindowsReport name a bad value: nothing here is
// silent, a span that is fully dropped is reported exactly like one that is only trimmed.
void clamp_highlight_spans(State& st, std::size_t line_len, std::string_view lang, int line_no) {
  Work& w = *st.w;
  std::stable_sort(w.hspans.begin(), w.hspans.end(),
                   [](const RolltuiMdHighlightSpan& a, const RolltuiMdHighlightSpan& b) { return a.begin < b.begin; });
  w.hruns.clear();
  auto report = [&](std::size_t sb, std::size_t se, const std::string& tail) {
    std::string& m = w.scratch;
    m.assign("\"");
    m.append(lang);
    m += "\" line ";
    m += std::to_string(line_no);
    m += ": span [";
    m += std::to_string(sb);
    m += ",";
    m += std::to_string(se);
    m += ") ";
    m += tail;
    rolltui_md_lines_add_clamped(st.out, m.data(), m.size());
  };
  std::size_t cursor = 0;
  for (const RolltuiMdHighlightSpan& s : w.hspans) {
    if (s.end <= s.begin) {
      report(s.begin, s.end, "runs backwards or is empty — dropped");
      continue;
    }
    if (s.begin >= line_len) {
      report(s.begin, s.end, "starts past the line's " + std::to_string(line_len) + " bytes — dropped");
      continue;
    }
    std::size_t b = s.begin;
    std::size_t e = s.end;
    std::string why;
    if (e > line_len) {
      e = line_len;
      why = "exceeds the line's " + std::to_string(line_len) + " bytes";
    }
    if (b < cursor) {
      b = cursor;
      if (!why.empty()) why += " and ";
      why += "overlaps an earlier span";
    }
    if (b >= e) {
      report(s.begin, s.end, "entirely overlapped by an earlier span — dropped");
      continue;
    }
    if (!why.empty())
      report(s.begin, s.end,
             why + " — clamped to [" + std::to_string(b) + "," + std::to_string(e) + ")");
    w.hruns.push_back({b, e, s.role});
    cursor = e;
  }
}

unsigned char highlight_role_at(const std::vector<HighlightRun>& runs, std::size_t offset, unsigned char fallback) {
  for (const HighlightRun& r : runs)
    if (r.begin <= offset && offset < r.end) return r.role;
  return fallback;
}

// Decides one numbered block's fold/cap state from the options and any override. Split
// out so the RULE reads as a rule: a threshold, then a state that may reverse it.
void decide_fold(const RolltuiMdRenderOptions* cf, std::size_t lines, CodeBlockInfo& info, std::size_t& cap) {
  cap = 0;
  if (!cf) return;
  const bool over = cf->fold_over_lines > 0 && lines > static_cast<std::size_t>(cf->fold_over_lines);
  bool folded = over;
  bool uncapped = false;
  for (std::size_t k = 0; k < cf->state_count; ++k)
    if (cf->states[k].index == info.index) {
      folded = cf->states[k].folded != 0;
      uncapped = cf->states[k].uncapped != 0;
      break;
    }
  info.folded = static_cast<unsigned char>(folded ? 1 : 0);
  // Foldable when the threshold says so OR a state folded it: either way it needs a
  // header row, because a folded block with nothing to click cannot be reopened.
  info.foldable = static_cast<unsigned char>((over || folded) ? 1 : 0);
  if (!folded && !uncapped && cf->cap_lines > 0 && lines > static_cast<std::size_t>(cf->cap_lines))
    cap = static_cast<std::size_t>(cf->cap_lines);
}

// `fold_index` < 0 means this block is NOT numbered and can never fold: the only such
// block is the pipe-table source a too-narrow table falls back to, which exists because
// of the WIDTH and so must not be able to own a user's fold toggle (Markdown.hpp).
void render_code(State& st, const std::string& code, const std::string& label, Ctx& ctx,
                 bool highlightable = false, long fold_index = -1) {
  Work& w = *st.w;
  const bool boxed = ctx.width >= 8;
  const int inner = boxed ? ctx.width - 4 : ctx.width;
  const std::string_view lang = highlightable ? first_word(label) : std::string_view{};
  const bool highlighting = highlightable && st.opt->highlight != nullptr;
  split_lines(w, code);
  // The ranges are read across calls that also use `w.code_lines`? They are not: nothing
  // below re-splits. Copied to a local count so the loop reads one thing.
  const std::size_t src_n = w.code_lines.size();
  WrapOptions wo;
  wo.ambiguous_wide = st.ambiguous;
  wo.tab_width = st.tab_width;
  bool first = true;
  auto raw_line = [&](std::size_t i) { return std::string_view(code).substr(w.code_lines[i].first, w.code_lines[i].second); };
  // A chrome row that still carries the block's own marker when it is the block's first.
  auto chrome_row = [&]() {
    std::size_t before = rolltui_md_lines_text_size(st.out);
    start_line(st, ctx, first);
    if (first) finish_chrome_first_line(st, ctx, before);
    first = false;
  };
  auto hrule = [&](const char* left, const char* right, std::string_view lab) {
    chrome_row();
    std::string& s = w.scratch;
    s.assign(left);
    int used = 1;
    if (!lab.empty()) {
      const int tw = unicode::display_width(lab, st.ambiguous) + 2;
      if (tw + 2 <= ctx.width) {
        s += ' ';
        s.append(lab);
        s += ' ';
        used += tw;
      }
    }
    for (; used < ctx.width - 1; ++used) s += "\xE2\x94\x80";  // ─
    s += right;
    span(st, s, st.roles().code_label);
    rolltui_md_lines_close(st.lines);
  };

  const bool numbered = fold_index >= 0;
  CodeBlockInfo info{};
  info.header_line = ROLLTUI_MD_NO_LINE;
  info.marker_line = ROLLTUI_MD_NO_LINE;
  std::size_t cap = 0;
  if (numbered) {
    info.index = static_cast<std::size_t>(fold_index);
    const std::string_view lw = first_word(label);
    info.lang_p = lw.data();
    info.lang_n = lw.size();
    info.lines = src_n;
    info.bytes = code.size();
    decide_fold(st.opt, src_n, info, cap);
  }
  // The header row IS the language label when there is one, so the box rule below drops
  // its own — one place names the block, and it does not move when the block opens.
  if (info.foldable) {
    info.header_line = rolltui_md_lines_count(st.lines);
    chrome_row();
    span(st, info.folded ? "\xE2\x96\xB8 " : "\xE2\x96\xBE ", st.roles().text_muted);  // ▸ ▾
    char buf[ROLLTUI_MD_SUMMARY_MAX];
    const std::size_t n =
        rolltui_md_code_block_summary(info.lang_p, info.lang_n, info.lines, info.bytes, buf, sizeof buf);
    span(st, std::string_view(buf, n), st.roles().code_label);
    rolltui_md_lines_close(st.lines);
  }
  // FOLDED AND CAPPED BLOCKS STILL CONTRIBUTE EVERY BYTE (Markdown.hpp): what is hidden
  // is lines, never text, so an offset into this document does not move when a block
  // opens or closes and a find highlight cannot land on the wrong bytes.
  auto emit_text = [&](std::string_view raw) {
    const std::size_t base = rolltui_md_lines_text_size(st.out);
    rolltui_md_lines_text_append(st.out, raw.data(), raw.size());
    ctx.end_logical_line(st);
    return base;
  };
  if (info.folded) {  // only ever set for a numbered block
    info.text_begin = rolltui_md_lines_text_size(st.out);
    for (std::size_t i = 0; i < src_n; ++i) emit_text(raw_line(i));
    info.text_end = rolltui_md_lines_text_size(st.out);
    info.hidden = src_n;
    rolltui_md_lines_add_code_block(st.out, &info);
    return;
  }

  if (boxed) hrule("\xE2\x94\x8C", "\xE2\x94\x90", info.foldable ? std::string_view{} : std::string_view(label));  // ┌ ┐
  info.text_begin = rolltui_md_lines_text_size(st.out);
  for (std::size_t i = 0; i < src_n; ++i) {
    const std::string_view raw = raw_line(i);
    const std::size_t base = emit_text(raw);
    if (cap > 0 && i >= cap) continue;  // hidden by the cap: text above, no lines drawn
    w.wrap.wrap(raw, inner, wo);
    // Unregistered highlighter (the default) or a non-highlightable block (HTML):
    // `hruns` stays empty and every grapheme below takes exactly the pre-seam path —
    // this is the control markdown_test.cpp asserts byte-identical.
    w.hruns.clear();
    if (highlighting) {
      w.block_lines.resize(src_n);
      for (std::size_t k = 0; k < src_n; ++k) {
        const std::string_view l = raw_line(k);
        w.block_lines[k] = {l.data(), l.size()};
      }
      w.hspans.clear();
      st.opt->highlight(st.opt->highlight_ctx, lang.data(), lang.size(), w.block_lines.data(), src_n, i,
                        [](void* sink, std::size_t begin, std::size_t end, unsigned char role) {
                          static_cast<std::vector<RolltuiMdHighlightSpan>*>(sink)->push_back({begin, end, role});
                        },
                        &w.hspans);
      clamp_highlight_spans(st, raw.size(), lang, static_cast<int>(i) + 1);
    }
    for (const Line& ln : w.wrap) {
      start_line(st, ctx, first);
      first = false;
      if (boxed) span(st, "\xE2\x94\x82 ", st.roles().code_label);  // │
      if (w.hruns.empty()) {
        w.srcs.clear();
        for (const WrapGrapheme& g : ln.graphemes) w.srcs.push_back(static_cast<std::uint32_t>(base + g.source_offset));
        span(st, ln.text, st.roles().code_block, w.srcs);
      } else {
        for (const WrapGrapheme& g : ln.graphemes) {
          const unsigned char r = highlight_role_at(w.hruns, g.source_offset, st.roles().code_block);
          const std::uint32_t one = static_cast<std::uint32_t>(base + g.source_offset);
          span(st, ln.text.substr(g.offset, g.length), r, std::span<const std::uint32_t>(&one, 1));
        }
      }
      const int pad = inner - ln.width;
      if (pad > 0) span(st, spaces(w, pad), st.roles().code_block);
      if (boxed) span(st, " \xE2\x94\x82", st.roles().code_label);
      rolltui_md_lines_close(st.lines);
    }
  }
  info.text_end = rolltui_md_lines_text_size(st.out);
  if (cap > 0 && src_n > cap) {
    info.hidden = src_n - cap;
    // The SAME marker the transcript and draw_scrolled_text use (Marker.hpp), so the
    // three cannot say "there is more below" three different ways.
    char mbuf[ROLLTUI_MARKER_MAX];
    const std::string_view marker(mbuf, rolltui_scroll_marker_text(info.hidden, inner, st.ambiguous, mbuf, sizeof mbuf));
    if (!marker.empty()) {
      info.marker_line = rolltui_md_lines_count(st.lines);
      chrome_row();
      if (boxed) span(st, "\xE2\x94\x82 ", st.roles().code_label);
      const int pad = inner - unicode::display_width(marker, st.ambiguous);
      if (pad > 0) span(st, spaces(w, pad), st.roles().code_block);
      span(st, marker, st.roles().scroll_marker);
      if (boxed) span(st, " \xE2\x94\x82", st.roles().code_label);
      rolltui_md_lines_close(st.lines);
    }
  }
  if (boxed) hrule("\xE2\x94\x94", "\xE2\x94\x98", {});  // └ ┘
  if (numbered) rolltui_md_lines_add_code_block(st.out, &info);
}

void runs_plain(std::string& out, const std::vector<Run>& runs) {
  out.clear();
  for (const Run& r : runs) out += r.text;
}

void render_table(State& st, const Block& t, Ctx& ctx);
void render_blocks(State& st, const std::vector<Block>& blocks, Ctx& ctx, bool tight);

// The next code-block number, or -1 when nobody is counting (a render with no options
// that need it). Counting here — at the BLOCK, not inside render_code — is what keeps
// the table fallback out of the sequence.
long next_code_index(State& st) { return static_cast<long>(st.code_index++); }

void render_block(State& st, const Block& b, Ctx& ctx) {
  using K = Block::Kind;
  Work& w = *st.w;
  switch (b.kind) {
    case K::Paragraph:
      layout_runs(st, b.inlines, ctx);
      break;
    case K::Heading: {
      // The heading marks are a Run like any other, and this is the one place that needs a
      // run list that is not the block's own. It lives in Work for the reason every buffer
      // here does: a local would be two allocations per heading per frame.
      const int level = std::clamp(b.level, 1, 6);
      w.heading_runs.clear();
      w.heading_runs.push_back({std::string(static_cast<std::size_t>(level), '#') + " ", kPlain, ""});
      w.heading_runs.insert(w.heading_runs.end(), b.inlines.begin(), b.inlines.end());
      layout_runs(st, w.heading_runs, ctx, 0, level + 1, st.roles().heading);
      break;
    }
    case K::Code:
      render_code(st, b.code, b.info, ctx, /*highlightable=*/true, next_code_index(st));
      break;
    case K::Html: {
      // Never highlighted: an HTML block is opaque code by design (Markdown.hpp), and it
      // carries no language tag to highlight it by. It IS numbered, because it is a block
      // of the document and folds like one.
      static const std::string kHtml = "html";
      render_code(st, b.code, kHtml, ctx, /*highlightable=*/false, next_code_index(st));
      break;
    }
    case K::Rule: {
      start_line(st, ctx, true);
      ctx.end_logical_line(st);
      std::string& s = w.scratch;
      s.clear();
      for (int i = 0; i < ctx.width; ++i) s += "\xE2\x94\x80";
      span(st, s, st.roles().rule);
      rolltui_md_lines_close(st.lines);
      break;
    }
    case K::Quote: {
      const PrefixPart bar = part(st, "\xE2\x94\x82 ", st.roles().quote);
      Ctx c = ctx.child(st, 2, std::span<const PrefixPart>(&bar, 1), std::span<const PrefixPart>(&bar, 1),
                        st.roles().quote);
      render_blocks(st, b.children, c, false);
      break;
    }
    case K::List: {
      unsigned n = b.start;
      int marker_w = 2;
      if (b.ordered) {
        const unsigned last = b.start + static_cast<unsigned>(b.children.size());
        marker_w = static_cast<int>(std::to_string(last).size()) + 2;
      }
      for (std::size_t i = 0; i < b.children.size(); ++i) {
        const Block& item = b.children[i];
        if (i > 0 && !b.tight) blank_line(st, ctx);
        std::string& m = w.scratch;
        m.clear();
        if (item.task) m = item.checked ? "[x] " : "[ ] ";
        else if (b.ordered) m = std::to_string(n) + ". ";
        else m = "\xE2\x80\xA2 ";  // •
        const int mw = unicode::display_width(m, st.ambiguous);
        const int width = std::max(mw, marker_w);
        m.insert(0, static_cast<std::size_t>(width - mw), ' ');  // right-align numbers under the widest
        const PrefixPart marker = part(st, m, st.roles().list_marker);
        const PrefixPart pad = part(st, spaces(w, width), ctx.base);
        Ctx c = ctx.child(st, width, std::span<const PrefixPart>(&marker, 1), std::span<const PrefixPart>(&pad, 1),
                          ctx.base);
        render_blocks(st, item.children, c, b.tight);
        if (item.children.empty()) {  // an empty item still shows its marker
          start_line(st, c, true);
          rolltui_md_lines_close(st.lines);
          c.end_logical_line(st);
        }
        ++n;
      }
      break;
    }
    case K::Item:  // only ever inside a List
      render_blocks(st, b.children, ctx, true);
      break;
    case K::Table:
      render_table(st, b, ctx);
      break;
  }
}

void render_blocks(State& st, const std::vector<Block>& blocks, Ctx& ctx, bool tight) {
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    if (i > 0 && !tight) blank_line(st, ctx);
    render_block(st, blocks[i], ctx);
  }
}

void render_table(State& st, const Block& t, Ctx& ctx) {
  Work& w = *st.w;
  const std::size_t cols = t.aligns.size();
  if (cols == 0 || t.rows.empty()) return;
  w.col_width.assign(cols, 1);
  for (const auto& row : t.rows)
    for (std::size_t c = 0; c < cols && c < row.size(); ++c) {
      runs_plain(w.cell_text, row[c]);
      w.col_width[c] = std::max(w.col_width[c], unicode::display_width(w.cell_text, st.ambiguous));
    }
  const int chrome = static_cast<int>(cols) * 3 + 1;  // "│ " per column + " │"
  if (chrome + static_cast<int>(cols) > ctx.width) {
    // Cannot show one cell per column: render the source instead, in full.
    std::string src;
    for (std::size_t r = 0; r < t.rows.size(); ++r) {
      src += "|";
      for (std::size_t c = 0; c < cols; ++c) {
        src += " ";
        if (c < t.rows[r].size()) {
          runs_plain(w.cell_text, t.rows[r][c]);
          src += w.cell_text;
        }
        src += " |";
      }
      src += "\n";
      if (r + 1 == t.head_rows) {
        src += "|";
        for (std::size_t c = 0; c < cols; ++c) src += "---|";
        src += "\n";
      }
    }
    static const std::string kTable = "table";
    render_code(st, src, kTable, ctx);
    return;
  }
  int total = chrome;
  for (int cw : w.col_width) total += cw;
  while (total > ctx.width) {  // shrink the widest column until it fits
    std::size_t widest = 0;
    for (std::size_t c = 1; c < cols; ++c)
      if (w.col_width[c] > w.col_width[widest]) widest = c;
    if (w.col_width[widest] <= 1) break;
    --w.col_width[widest];
    --total;
  }
  bool first = true;
  auto border = [&](const char* l, const char* m, const char* r) {
    const std::size_t before = rolltui_md_lines_text_size(st.out);
    start_line(st, ctx, first);
    if (first) finish_chrome_first_line(st, ctx, before);
    first = false;
    std::string& s = w.scratch;
    s.assign(l);
    for (std::size_t c = 0; c < cols; ++c) {
      for (int i = 0; i < w.col_width[c] + 2; ++i) s += "\xE2\x94\x80";
      s += (c + 1 < cols) ? m : r;
    }
    span(st, s, st.roles().table_border);
    rolltui_md_lines_close(st.lines);
  };
  border("\xE2\x94\x8C", "\xE2\x94\xAC", "\xE2\x94\x90");  // ┌ ┬ ┐
  RolltuiMdLines* aux = rolltui_md_lines_aux(st.out);
  for (std::size_t r = 0; r < t.rows.size(); ++r) {
    const bool head = r < t.head_rows;
    // The cells are laid out at their COLUMN's width, in a store of their own, and then
    // poured into the row: the row's lines are appended AFTER the cell lines they read,
    // so a mark/rewind on one store could not drop the cells (rolltui_md_lines.h).
    rolltui_md_lines_reset(aux);
    w.cell_at.assign(cols, {0, 0});
    std::size_t height = 1;
    for (std::size_t c = 0; c < cols; ++c) {
      Ctx cc = ctx;
      cc.pf_len = 0;
      cc.pr_len = 0;
      cc.first_used = false;
      cc.width = w.col_width[c];
      cc.base = head ? st.roles().table_header : ctx.base;
      cc.terminator = "\t";  // cells of a row share one logical line
      const std::size_t before = rolltui_md_lines_count(aux);
      st.lines = aux;
      if (c < t.rows[r].size())
        layout_runs(st, t.rows[r][c], cc, 0, 0, head ? st.roles().table_header : ROLLTUI_MD_NO_ROLE);
      else
        cc.end_logical_line(st);  // an absent cell still holds its column
      st.lines = st.out;
      w.cell_at[c] = {before, rolltui_md_lines_count(aux) - before};
      height = std::max(height, w.cell_at[c].second == 0 ? std::size_t{1} : w.cell_at[c].second);
    }
    rolltui_md_lines_finish(aux);
    const std::size_t tn = rolltui_md_lines_text_size(st.out);
    if (tn > 0 && rolltui_md_lines_text(st.out)[tn - 1] == '\t') {
      rolltui_md_lines_text_pop(st.out);
      rolltui_md_lines_text_append(st.out, "\n", 1);
    }
    for (std::size_t h = 0; h < height; ++h) {
      start_line(st, ctx);
      span(st, "\xE2\x94\x82", st.roles().table_border);
      for (std::size_t c = 0; c < cols; ++c) {
        span(st, " ", st.roles().table_border);
        const RolltuiMdLine* cell = h < w.cell_at[c].second ? rolltui_md_lines_line(aux, w.cell_at[c].first + h) : nullptr;
        const int pad = w.col_width[c] - (cell ? cell->width : 0);
        int left = 0;
        if (t.aligns[c] == Align::Right) left = pad;
        else if (t.aligns[c] == Align::Center) left = pad / 2;
        if (left > 0) span(st, spaces(w, left), ctx.base);
        if (cell)
          for (const Span& s : cell->spans())
            span(st, s.text(), s.role, s.sources(), s.href());
        if (pad - left > 0) span(st, spaces(w, pad - left), ctx.base);
        span(st, " \xE2\x94\x82", st.roles().table_border);
      }
      rolltui_md_lines_close(st.lines);
    }
    if (r + 1 == t.head_rows && r + 1 < t.rows.size())
      border("\xE2\x94\x9C", "\xE2\x94\xBC", "\xE2\x94\xA4");  // ├ ┼ ┤
  }
  border("\xE2\x94\x94", "\xE2\x94\xB4", "\xE2\x94\x98");  // └ ┴ ┘
}

// OWNED, and never a hand-rolled `new`: the same `make_unique(...).release()` shape every
// other C++ implementation behind these boundaries uses to hand a handle over
// (`rolltui_diff_scratch_new`, `rolltui_wrap_new`), which is what `ownership_test` demands.
void* work_make() { return std::make_unique<Work>().release(); }
void work_destroy(void* p) { std::unique_ptr<Work>(static_cast<Work*>(p)).reset(); }

}  // namespace

// ---- the boundary ---------------------------------------------------------------------

// The block tree in the shape C++ writes one: a vector of blocks that own their children.
// The C's is index arrays with a child list; NEITHER is in the public header, which is the
// port's one real simplification (rolltui/c/rolltui_markdown.h).
struct RolltuiMdDoc {
  std::vector<Block> blocks;
};

extern "C" RolltuiMdDoc* rolltui_md_doc_new() { return std::make_unique<RolltuiMdDoc>().release(); }
extern "C" void rolltui_md_doc_free(RolltuiMdDoc* d) { std::unique_ptr<RolltuiMdDoc>(d).reset(); }

extern "C" unsigned rolltui_md_parser_flags() { return parser_flags(); }

extern "C" size_t rolltui_md_decode_entity(const char* ent, size_t n, char* out, size_t cap) {
  const std::string s = decode_entity(std::string_view(ent, n));
  const std::size_t take = std::min(s.size(), cap);
  std::memcpy(out, s.data(), take);
  return take;
}

extern "C" void rolltui_md_parse(RolltuiMdDoc* d, const char* src, size_t n) {
  // The vector is CLEARED and refilled, which is the right reset here and not the wrong one
  // Phase 13 found four times: a Block owns strings and vectors, so `clear()` frees exactly
  // the storage a re-parse would reuse — but a re-parse is a DIFFERENT document, whose
  // blocks bear no relation to the last one's, so there is nothing being reused to free.
  d->blocks.clear();
  ParseState st;
  st.blocks = &d->blocks;
  MD_PARSER parser{};
  parser.abi_version = 0;
  parser.flags = parser_flags();
  parser.enter_block = enter_block;
  parser.leave_block = leave_block;
  parser.enter_span = enter_span;
  parser.leave_span = leave_span;
  parser.text = text;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;
  md_parse(src, static_cast<MD_SIZE>(n), &parser, &st);
}

extern "C" size_t rolltui_md_doc_block_count(const RolltuiMdDoc* d) { return d->blocks.size(); }

extern "C" unsigned char rolltui_md_doc_block_kind(const RolltuiMdDoc* d, size_t i) {
  return static_cast<unsigned char>(d->blocks[i].kind);
}

extern "C" const char* rolltui_md_doc_block_code(const RolltuiMdDoc* d, size_t i, size_t* n) {
  if (n) *n = d->blocks[i].code.size();
  return d->blocks[i].code.data();
}

extern "C" size_t rolltui_md_code_block_summary(const char* lang, size_t lang_n, size_t lines, size_t bytes,
                                                char* out, size_t cap) {
  // A bare fence has no language to name, so it is called "code". The buffer is a
  // constraint on THIS function rather than on a caller's data.
  if (lang_n == 0) {
    lang = "code";
    lang_n = 4;
  }
  const double kb = static_cast<double>(bytes) / 1024.0;
  char size_buf[32];
  if (bytes < 1024) std::snprintf(size_buf, sizeof size_buf, "%zu B", bytes);
  else if (bytes < 1024 * 1024) std::snprintf(size_buf, sizeof size_buf, "%.1f kB", kb);
  else std::snprintf(size_buf, sizeof size_buf, "%.1f MB", kb / 1024.0);
  const int n = std::snprintf(out, cap, "%.*s \xC2\xB7 %zu %s \xC2\xB7 %s", static_cast<int>(lang_n), lang, lines,
                              lines == 1 ? "line" : "lines", size_buf);
  if (n < 0) return 0;
  return static_cast<std::size_t>(n) < cap ? static_cast<std::size_t>(n) : (cap ? cap - 1 : 0);
}

extern "C" void rolltui_md_render(RolltuiMdLines* out, const RolltuiMdDoc* doc,
                                  const RolltuiMdRenderOptions* opt) {
  RolltuiMdRenderOptions defaults{};
  if (!opt) opt = &defaults;
  rolltui_md_lines_reset(out);
  Work* w = static_cast<Work*>(rolltui_md_lines_work(out, work_make, work_destroy));
  if (w->busy) {
    std::fprintf(stderr, "rolltui: markdown: a render re-entered the same store\n");
    std::abort();
  }
  w->busy = true;
  w->clear();
  State st;
  st.out = out;
  st.lines = out;
  st.w = w;
  st.opt = opt;
  st.ambiguous = opt->ambiguous_wide != 0;
  st.tab_width = opt->tab_width;
  Ctx ctx;
  ctx.width = std::max(opt->width, 1);
  ctx.base = opt->base;
  render_blocks(st, doc->blocks, ctx, false);
  const std::size_t n = rolltui_md_lines_text_size(out);
  if (n > 0 && rolltui_md_lines_text(out)[n - 1] == '\n') rolltui_md_lines_text_pop(out);
  rolltui_md_lines_finish(out);
  w->busy = false;
}
