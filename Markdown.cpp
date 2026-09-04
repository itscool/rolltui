// rolltui/Markdown.cpp — the SHIM over `rolltui/c/rolltui_markdown.h`: the RAII, the
// styling vocabulary, and the translation of one `std::function` into a function pointer.
// The two implementations live in `MarkdownCpp.cpp` and `c/rolltui_markdown.c`, and
// `-DROLLTUI_C` picks which one links (Phase 15 m4).
//
// Nothing here decides anything. It exists so that the C boundary never has to know what a
// `Role` is called, what a `std::function` is, or how a caller wants its bytes owned.
#include "rolltui/Markdown.hpp"

#include <algorithm>
#include <cstring>

namespace rolltui::markdown {

namespace {

// THE STYLING VOCABULARY, HANDED IN. `rolltui/Style.hpp` is the one place these names
// exist; a renderer is told which byte to tag its output with, exactly as `Diff.cpp` tells
// the diff colouriser (Phase 15 m2's rule, and the reason neither implementation names a
// role).
constexpr RolltuiMdRoles kRoles = {
    /*text_muted=*/static_cast<unsigned char>(Role::text_muted),
    /*heading=*/static_cast<unsigned char>(Role::md_heading),
    /*emphasis=*/static_cast<unsigned char>(Role::md_emphasis),
    /*strong=*/static_cast<unsigned char>(Role::md_strong),
    /*strikethrough=*/static_cast<unsigned char>(Role::md_strikethrough),
    /*code_inline=*/static_cast<unsigned char>(Role::md_code_inline),
    /*code_block=*/static_cast<unsigned char>(Role::md_code_block),
    /*code_label=*/static_cast<unsigned char>(Role::md_code_label),
    /*link=*/static_cast<unsigned char>(Role::md_link),
    /*link_url=*/static_cast<unsigned char>(Role::md_link_url),
    /*quote=*/static_cast<unsigned char>(Role::md_quote),
    /*list_marker=*/static_cast<unsigned char>(Role::md_list_marker),
    /*table_border=*/static_cast<unsigned char>(Role::md_table_border),
    /*table_header=*/static_cast<unsigned char>(Role::md_table_header),
    /*rule=*/static_cast<unsigned char>(Role::md_rule),
    /*scroll_marker=*/static_cast<unsigned char>(Role::scroll_marker),
};
static_assert(static_cast<unsigned char>(Role::count_) < ROLLTUI_MD_NO_ROLE,
              "the no-override sentinel must not be able to collide with a real Role");

// What the boundary carries instead of a `std::function`: the host's callable, plus the two
// buffers the translation needs. Both belong to the `Rendered` doing the render, so a
// highlighted block costs no allocation after the first.
struct HighlightCtx {
  const Highlighter* fn;
  std::vector<std::string_view>* lines;
  std::vector<HighlightSpan>* spans;
};

// The trampoline: one call of the host's callable, its answer poured through the sink.
void call_highlighter(void* ctx, const char* lang, std::size_t lang_n, const RolltuiMdCodeLine* lines,
                      std::size_t line_count, std::size_t index, RolltuiMdSpanSink emit, void* sink) {
  HighlightCtx& h = *static_cast<HighlightCtx*>(ctx);
  h.lines->resize(line_count);
  for (std::size_t i = 0; i < line_count; ++i) (*h.lines)[i] = std::string_view(lines[i].p, lines[i].n);
  *h.spans = (*h.fn)(std::string_view(lang, lang_n), std::span<const std::string_view>(*h.lines), index);
  for (const HighlightSpan& s : *h.spans) emit(sink, s.begin, s.end, static_cast<unsigned char>(s.role));
}

}  // namespace

// ---- the document ---------------------------------------------------------------------

void Document::parse(std::string_view source) {
  if (!doc_) doc_.reset(rolltui_md_doc_new());
  rolltui_md_parse(doc_.get(), source.data(), source.size());
}

Document parse(std::string_view source) {
  Document d;
  d.parse(source);
  return d;
}

std::string decode_entity(std::string_view entity) {
  // The C's size rule: the buffer must hold max(n, 4), because an entity it does not
  // recognise is copied through verbatim rather than dropped.
  std::string out(entity.size() < 4 ? 4 : entity.size(), '\0');
  out.resize(rolltui_md_decode_entity(entity.data(), entity.size(), out.data(), out.size()));
  return out;
}

unsigned parser_flags() { return rolltui_md_parser_flags(); }

// ---- rendering ---------------------------------------------------------------------

void Rendered::render(const Document& doc, const RenderOptions& opt) {
  RolltuiMdLines* S = store();
  if (!doc.handle()) {  // never parsed into: an empty document draws nothing
    rolltui_md_lines_reset(S);
    rolltui_md_lines_finish(S);
    return;
  }
  RolltuiMdRenderOptions o{};
  o.width = opt.width;
  o.ambiguous_wide = opt.ambiguous_wide ? 1 : 0;
  o.tab_width = opt.tab_width;
  o.base = static_cast<unsigned char>(opt.base);
  o.roles = kRoles;
  o.fold_over_lines = opt.code_fold.fold_over_lines;
  o.cap_lines = opt.code_fold.cap_lines;
  // The overrides cross as a BORROW for the duration of the call, the same shape as every
  // other array on these boundaries. A host sets a handful of them at most, so the vector
  // below is built only when there are any and is not on a per-frame path otherwise.
  std::vector<RolltuiMdFoldState> states;
  if (!opt.code_fold.states.empty()) {
    states.reserve(opt.code_fold.states.size());
    for (const CodeFoldState& s : opt.code_fold.states)
      states.push_back({s.index, static_cast<unsigned char>(s.folded), static_cast<unsigned char>(s.uncapped)});
    o.states = states.data();
    o.state_count = states.size();
  }
  HighlightCtx hc{&opt.highlight, &hl_lines_, &hl_spans_};
  if (opt.highlight) {  // unset is the seam's whole opt-in: never registered, never called
    o.highlight = call_highlighter;
    o.highlight_ctx = &hc;
  }
  rolltui_md_render(S, doc.handle(), &o);
}

void Rendered::render(std::string_view source, const RenderOptions& opt) { render(parse(source), opt); }

Rendered render_text(const Document& doc, const RenderOptions& opt) {
  Rendered r;
  r.render(doc, opt);
  return r;
}

Rendered render_text(std::string_view source, const RenderOptions& opt) { return render_text(parse(source), opt); }

std::size_t code_block_summary(std::string_view lang, std::size_t lines, std::size_t bytes, char* out,
                               std::size_t cap) {
  return rolltui_md_code_block_summary(lang.data(), lang.size(), lines, bytes, out, cap);
}

std::string plain_text(std::span<const StyledLine> lines) {
  std::string s;
  plain_text_into(lines, s);
  return s;
}

std::string plain_text(const StyledLine& line) { return plain_text(std::span<const StyledLine>(&line, 1)); }

void plain_text_into(std::span<const StyledLine> lines, std::string& out) {
  out.clear();
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i) out += '\n';
    for (const Span& sp : lines[i].spans()) out.append(sp.text());
  }
}

}  // namespace rolltui::markdown
