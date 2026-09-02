// rolltui/Markdown.cpp — md4c → block tree → styled wrapped lines. Contract in
// Markdown.hpp. md4c (rolltui/third_party/md4c, MIT, pinned in its README) is included
// here and nowhere else.
#include "rolltui/Markdown.hpp"

#include <algorithm>
#include <cstring>

#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"
#include "rolltui/third_party/md4c/md4c.h"

namespace rolltui::markdown {

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

namespace {

struct ParseState {
  Document doc;
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
    std::vector<Block>& into = parent ? parent->children : doc.blocks;
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

}  // namespace

Document parse(std::string_view source) {
  ParseState st;
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
  md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &st);
  return std::move(st.doc);
}

// ---- rendering ---------------------------------------------------------------------

namespace {

Role role_for(unsigned style, Role base) {
  if (style & kCode) return Role::md_code_inline;
  if (style & kLinkUrl) return Role::md_link_url;
  if (style & kLink) return Role::md_link;
  if (style & kStrike) return Role::md_strikethrough;
  if (style & kStrong) return Role::md_strong;
  if (style & kEmphasis) return Role::md_emphasis;
  if (style & kUnderline) return Role::md_link;
  return base;
}

// Appends text to a line as a span. `sources` has one offset per grapheme cluster of
// `text` (chrome when empty: every grapheme kNoSource). Adjacent spans merge only when
// role and href both match, so a link's cells stay one span.
void push_span(StyledLine& line, std::string text, Role role, bool ambiguous,
               std::vector<std::uint32_t> sources = {}, const std::string& href = "") {
  if (text.empty()) return;
  int w = 0;
  std::size_t clusters = 0;
  for (const unicode::Grapheme& g : unicode::graphemes(text, ambiguous)) { w += g.width; ++clusters; }
  if (sources.size() != clusters) sources.assign(clusters, kNoSource);
  if (!line.spans.empty() && line.spans.back().role == role && line.spans.back().href == href) {
    Span& s = line.spans.back();
    s.text += text;
    s.width += w;
    s.sources.insert(s.sources.end(), sources.begin(), sources.end());
  } else {
    line.spans.push_back({std::move(text), w, role, href, std::move(sources)});
  }
  line.width += w;
}

// Consecutive logical offsets for `text` appended to the logical text at `base`.
std::vector<std::uint32_t> sources_for(const std::string& text, std::size_t base, bool ambiguous) {
  std::vector<std::uint32_t> s;
  for (const unicode::Grapheme& g : unicode::graphemes(text, ambiguous))
    s.push_back(static_cast<std::uint32_t>(base + g.offset));
  return s;
}

std::string spaces(int n) { return std::string(static_cast<std::size_t>(n > 0 ? n : 0), ' '); }

// Rendering context: the width available for content, the prefix each line carries
// (a marker on the first line, indentation on the rest), and the logical text being
// accumulated (Markdown.hpp, "LOGICAL TEXT").
struct Ctx {
  int width;
  std::vector<Span> prefix_first;
  std::vector<Span> prefix_rest;
  bool first_used = false;
  Role base;
  bool ambiguous;
  int tab_width;
  std::string* logical = nullptr;  // the document's logical text
  const char* terminator = "\n";   // what ends a block's logical line ("\t" inside a table row)

  std::vector<Span> take_prefix() {
    if (!first_used) { first_used = true; return prefix_first; }
    return prefix_rest;
  }
  Ctx child(int less, std::vector<Span> first_extra, std::vector<Span> rest_extra, Role new_base) {
    Ctx c = *this;
    c.width = width - less;
    if (c.width < 1) c.width = 1;
    c.prefix_first = take_prefix();
    c.prefix_first.insert(c.prefix_first.end(), first_extra.begin(), first_extra.end());
    c.prefix_rest = prefix_rest;
    c.prefix_rest.insert(c.prefix_rest.end(), rest_extra.begin(), rest_extra.end());
    c.first_used = false;
    c.base = new_base;
    return c;
  }
  void end_logical_line() { *logical += terminator; }
};

// Starts a line with the context's prefix. On a block's first line the prefix is
// logical text (the marker is copied with the item); elsewhere it is chrome.
StyledLine start_line(Ctx& ctx, bool first_of_block = false) {
  StyledLine l;
  for (const Span& s : ctx.take_prefix()) {
    if (first_of_block) {
      std::size_t base = ctx.logical->size();
      *ctx.logical += s.text;
      push_span(l, s.text, s.role, ctx.ambiguous, sources_for(s.text, base, ctx.ambiguous));
    } else {
      push_span(l, s.text, s.role, ctx.ambiguous);
    }
  }
  return l;
}

// A chrome-only first line (a code box's top rule, a table's top border) still carries
// the block's marker; the marker then stands on a logical line of its own.
void finish_chrome_first_line(Ctx& ctx, std::size_t logical_before) {
  if (ctx.logical->size() > logical_before) ctx.end_logical_line();
}

void blank_line(std::vector<StyledLine>& out, Ctx& ctx) {
  StyledLine l = start_line(ctx);
  // Trim trailing spaces so a blank line inside a list is empty and a blank line
  // inside a quote is just its bar.
  while (!l.spans.empty()) {
    Span& s = l.spans.back();
    std::size_t keep = s.text.find_last_not_of(' ');
    if (keep == std::string::npos) {
      l.width -= s.width;
      l.spans.pop_back();
      continue;
    }
    if (keep + 1 < s.text.size()) {
      int dropped = static_cast<int>(s.text.size() - keep - 1);
      s.text.resize(keep + 1);
      s.width -= dropped;
      l.width -= dropped;
      s.sources.assign(unicode::graphemes(s.text, ctx.ambiguous).size(), kNoSource);
    }
    break;
  }
  ctx.end_logical_line();
  out.push_back(std::move(l));
}

// Lay out inline runs as wrapped lines with per-grapheme roles and hrefs. The runs'
// concatenated text becomes one logical line.
void layout_runs(const std::vector<Run>& runs, Ctx& ctx, std::vector<StyledLine>& out,
                 int first_indent = 0, int hanging_indent = 0, Role role_override = Role::count_) {
  std::string text;
  struct RunAt { std::size_t start; Role role; const std::string* href; };
  std::vector<RunAt> at;
  for (const Run& r : runs) {
    Role role = role_override != Role::count_ ? role_override : role_for(r.style, ctx.base);
    at.push_back({text.size(), role, (r.style & kLink) ? &r.href : nullptr});
    text += r.text;
  }
  WrapOptions wo;
  wo.ambiguous_wide = ctx.ambiguous;
  wo.tab_width = ctx.tab_width;
  wo.first_indent = first_indent;
  wo.hanging_indent = hanging_indent;
  std::vector<Line> lines = wrap(text, ctx.width, wo);
  static const std::string kNoHref;
  auto run_at = [&](std::size_t off) -> const RunAt* {
    const RunAt* r = nullptr;
    for (const RunAt& a : at) {
      if (a.start <= off) r = &a;
      else break;
    }
    return r;
  };
  std::size_t base = 0;
  for (std::size_t k = 0; k < lines.size(); ++k) {
    const Line& ln = lines[k];
    StyledLine sl = start_line(ctx, k == 0);
    if (k == 0) {
      base = ctx.logical->size();
      *ctx.logical += text;
    }
    if (ln.indent > 0) push_span(sl, spaces(ln.indent), ctx.base, ctx.ambiguous);
    for (const WrapGrapheme& g : ln.graphemes) {
      const RunAt* r = run_at(g.source_offset);
      push_span(sl, ln.text.substr(g.offset, g.length), r ? r->role : ctx.base, ctx.ambiguous,
                {static_cast<std::uint32_t>(base + g.source_offset)}, (r && r->href) ? *r->href : kNoHref);
    }
    out.push_back(std::move(sl));
  }
  ctx.end_logical_line();
}

std::vector<std::string> split_lines(const std::string& code) {
  std::vector<std::string> v;
  std::size_t start = 0;
  while (start <= code.size()) {
    std::size_t nl = code.find('\n', start);
    if (nl == std::string::npos) {
      if (start < code.size()) v.push_back(code.substr(start));
      break;
    }
    v.push_back(code.substr(start, nl - start));
    start = nl + 1;
  }
  return v;
}

void render_code(const std::string& code, const std::string& label, Ctx& ctx,
                 std::vector<StyledLine>& out) {
  const bool boxed = ctx.width >= 8;
  const int inner = boxed ? ctx.width - 4 : ctx.width;
  WrapOptions wo;
  wo.ambiguous_wide = ctx.ambiguous;
  wo.tab_width = ctx.tab_width;
  bool first = true;
  auto hrule = [&](const char* left, const char* right, const std::string& lab) {
    std::size_t before = ctx.logical->size();
    StyledLine l = start_line(ctx, first);
    if (first) finish_chrome_first_line(ctx, before);
    first = false;
    std::string s = left;
    int used = 1;
    if (!lab.empty()) {
      std::string t = " " + lab + " ";
      int tw = unicode::display_width(t, ctx.ambiguous);
      if (tw + 2 <= ctx.width) { s += t; used += tw; }
    }
    for (; used < ctx.width - 1; ++used) s += "\xE2\x94\x80";  // ─
    s += right;
    push_span(l, s, Role::md_code_label, ctx.ambiguous);
    out.push_back(std::move(l));
  };
  if (boxed) hrule("\xE2\x94\x8C", "\xE2\x94\x90", label);  // ┌ ┐
  for (const std::string& raw : split_lines(code)) {
    std::vector<Line> lines = wrap(raw, inner, wo);
    std::size_t base = ctx.logical->size();
    *ctx.logical += raw;
    ctx.end_logical_line();
    for (const Line& ln : lines) {
      StyledLine sl = start_line(ctx, first);
      first = false;
      if (boxed) push_span(sl, "\xE2\x94\x82 ", Role::md_code_label, ctx.ambiguous);  // │
      std::vector<std::uint32_t> src;
      for (const WrapGrapheme& g : ln.graphemes) src.push_back(static_cast<std::uint32_t>(base + g.source_offset));
      push_span(sl, ln.text, Role::md_code_block, ctx.ambiguous, std::move(src));
      int pad = inner - ln.width;
      if (pad > 0) push_span(sl, spaces(pad), Role::md_code_block, ctx.ambiguous);
      if (boxed) push_span(sl, " \xE2\x94\x82", Role::md_code_label, ctx.ambiguous);
      out.push_back(std::move(sl));
    }
  }
  if (boxed) hrule("\xE2\x94\x94", "\xE2\x94\x98", "");  // └ ┘
}

std::string runs_plain(const std::vector<Run>& runs) {
  std::string s;
  for (const Run& r : runs) s += r.text;
  return s;
}

void render_table(const Block& t, Ctx& ctx, std::vector<StyledLine>& out);
void render_blocks(const std::vector<Block>& blocks, Ctx& ctx, std::vector<StyledLine>& out, bool tight);

void render_block(const Block& b, Ctx& ctx, std::vector<StyledLine>& out) {
  using K = Block::Kind;
  switch (b.kind) {
    case K::Paragraph:
      layout_runs(b.inlines, ctx, out);
      break;
    case K::Heading: {
      std::vector<Run> runs;
      int level = std::clamp(b.level, 1, 6);
      runs.push_back({std::string(static_cast<std::size_t>(level), '#') + " ", kPlain, ""});
      runs.insert(runs.end(), b.inlines.begin(), b.inlines.end());
      layout_runs(runs, ctx, out, 0, level + 1, Role::md_heading);
      break;
    }
    case K::Code:
      render_code(b.code, b.info, ctx, out);
      break;
    case K::Html:
      render_code(b.code, "html", ctx, out);
      break;
    case K::Rule: {
      StyledLine l = start_line(ctx, true);
      ctx.end_logical_line();
      std::string s;
      for (int i = 0; i < ctx.width; ++i) s += "\xE2\x94\x80";
      push_span(l, s, Role::md_rule, ctx.ambiguous);
      out.push_back(std::move(l));
      break;
    }
    case K::Quote: {
      Span bar{"\xE2\x94\x82 ", 2, Role::md_quote, {}, {}};
      Ctx c = ctx.child(2, {bar}, {bar}, Role::md_quote);
      render_blocks(b.children, c, out, false);
      break;
    }
    case K::List: {
      unsigned n = b.start;
      int marker_w = 2;
      if (b.ordered) {
        unsigned last = b.start + static_cast<unsigned>(b.children.size());
        marker_w = static_cast<int>(std::to_string(last).size()) + 2;
      }
      for (std::size_t i = 0; i < b.children.size(); ++i) {
        const Block& item = b.children[i];
        if (i > 0 && !b.tight) blank_line(out, ctx);
        std::string m;
        if (item.task) m = item.checked ? "[x] " : "[ ] ";
        else if (b.ordered) m = std::to_string(n) + ". ";
        else m = "\xE2\x80\xA2 ";  // •
        int mw = unicode::display_width(m, ctx.ambiguous);
        int w = std::max(mw, marker_w);
        m = spaces(w - mw) + m;  // right-align numbers under the widest
        Span marker{m, w, Role::md_list_marker, {}, {}};
        Span pad{spaces(w), w, ctx.base, {}, {}};
        Ctx c = ctx.child(w, {marker}, {pad}, ctx.base);
        render_blocks(item.children, c, out, b.tight);
        if (item.children.empty()) {  // an empty item still shows its marker
          out.push_back(start_line(c, true));
          c.end_logical_line();
        }
        ++n;
      }
      break;
    }
    case K::Item:  // only ever inside a List
      render_blocks(b.children, ctx, out, true);
      break;
    case K::Table:
      render_table(b, ctx, out);
      break;
  }
}

void render_blocks(const std::vector<Block>& blocks, Ctx& ctx, std::vector<StyledLine>& out, bool tight) {
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    if (i > 0 && !tight) blank_line(out, ctx);
    render_block(blocks[i], ctx, out);
  }
}

void render_table(const Block& t, Ctx& ctx, std::vector<StyledLine>& out) {
  const std::size_t cols = t.aligns.size();
  if (cols == 0 || t.rows.empty()) return;
  std::vector<int> natural(cols, 1);
  for (const auto& row : t.rows)
    for (std::size_t c = 0; c < cols && c < row.size(); ++c)
      natural[c] = std::max(natural[c], unicode::display_width(runs_plain(row[c]), ctx.ambiguous));
  const int chrome = static_cast<int>(cols) * 3 + 1;  // "│ " per column + " │"
  if (chrome + static_cast<int>(cols) > ctx.width) {
    // Cannot show one cell per column: render the source instead, in full.
    std::string src;
    for (std::size_t r = 0; r < t.rows.size(); ++r) {
      std::string line = "|";
      for (std::size_t c = 0; c < cols; ++c)
        line += " " + (c < t.rows[r].size() ? runs_plain(t.rows[r][c]) : std::string()) + " |";
      src += line + "\n";
      if (r + 1 == t.head_rows) {
        std::string sep = "|";
        for (std::size_t c = 0; c < cols; ++c) sep += "---|";
        src += sep + "\n";
      }
    }
    render_code(src, "table", ctx, out);
    return;
  }
  std::vector<int> cw = natural;
  int total = chrome;
  for (int w : cw) total += w;
  while (total > ctx.width) {  // shrink the widest column until it fits
    std::size_t widest = 0;
    for (std::size_t c = 1; c < cols; ++c) if (cw[c] > cw[widest]) widest = c;
    if (cw[widest] <= 1) break;
    --cw[widest];
    --total;
  }
  bool first = true;
  auto border = [&](const char* l, const char* m, const char* r) {
    std::size_t before = ctx.logical->size();
    StyledLine ln = start_line(ctx, first);
    if (first) finish_chrome_first_line(ctx, before);
    first = false;
    std::string s = l;
    for (std::size_t c = 0; c < cols; ++c) {
      for (int i = 0; i < cw[c] + 2; ++i) s += "\xE2\x94\x80";
      s += (c + 1 < cols) ? m : r;
    }
    push_span(ln, s, Role::md_table_border, ctx.ambiguous);
    out.push_back(std::move(ln));
  };
  border("\xE2\x94\x8C", "\xE2\x94\xAC", "\xE2\x94\x90");  // ┌ ┬ ┐
  for (std::size_t r = 0; r < t.rows.size(); ++r) {
    const bool head = r < t.head_rows;
    std::vector<std::vector<StyledLine>> cells(cols);
    std::size_t height = 1;
    for (std::size_t c = 0; c < cols; ++c) {
      Ctx cc = ctx;
      cc.prefix_first.clear();
      cc.prefix_rest.clear();
      cc.first_used = false;
      cc.width = cw[c];
      cc.base = head ? Role::md_table_header : ctx.base;
      cc.terminator = "\t";  // cells of a row share one logical line
      if (c < t.rows[r].size())
        layout_runs(t.rows[r][c], cc, cells[c], 0, 0, head ? Role::md_table_header : Role::count_);
      else
        cc.end_logical_line();  // an absent cell still holds its column
      if (cells[c].empty()) cells[c].push_back({});
      height = std::max(height, cells[c].size());
    }
    if (!ctx.logical->empty() && ctx.logical->back() == '\t') ctx.logical->back() = '\n';
    for (std::size_t h = 0; h < height; ++h) {
      StyledLine ln = start_line(ctx);
      push_span(ln, "\xE2\x94\x82", Role::md_table_border, ctx.ambiguous);
      for (std::size_t c = 0; c < cols; ++c) {
        push_span(ln, " ", Role::md_table_border, ctx.ambiguous);
        StyledLine cell = h < cells[c].size() ? cells[c][h] : StyledLine{};
        int pad = cw[c] - cell.width;
        int left = 0;
        if (t.aligns[c] == Align::Right) left = pad;
        else if (t.aligns[c] == Align::Center) left = pad / 2;
        if (left > 0) push_span(ln, spaces(left), ctx.base, ctx.ambiguous);
        for (const Span& s : cell.spans) push_span(ln, s.text, s.role, ctx.ambiguous, s.sources, s.href);
        if (pad - left > 0) push_span(ln, spaces(pad - left), ctx.base, ctx.ambiguous);
        push_span(ln, " \xE2\x94\x82", Role::md_table_border, ctx.ambiguous);
      }
      out.push_back(std::move(ln));
    }
    if (r + 1 == t.head_rows && r + 1 < t.rows.size())
      border("\xE2\x94\x9C", "\xE2\x94\xBC", "\xE2\x94\xA4");  // ├ ┼ ┤
  }
  border("\xE2\x94\x94", "\xE2\x94\xB4", "\xE2\x94\x98");  // └ ┴ ┘
}

}  // namespace

Rendered render_text(const Document& doc, const RenderOptions& opt) {
  Rendered r;
  Ctx ctx{std::max(opt.width, 1), {}, {}, false, opt.base, opt.ambiguous_wide, opt.tab_width, &r.text, "\n"};
  render_blocks(doc.blocks, ctx, r.lines, false);
  if (!r.text.empty() && r.text.back() == '\n') r.text.pop_back();
  return r;
}

Rendered render_text(std::string_view source, const RenderOptions& opt) {
  return render_text(parse(source), opt);
}

std::vector<StyledLine> render(const Document& doc, const RenderOptions& opt) {
  return render_text(doc, opt).lines;
}

std::vector<StyledLine> render(std::string_view source, const RenderOptions& opt) {
  return render(parse(source), opt);
}

std::string plain_text(const std::vector<StyledLine>& lines) {
  std::string s;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i) s += '\n';
    for (const Span& sp : lines[i].spans) s += sp.text;
  }
  return s;
}

}  // namespace rolltui::markdown
