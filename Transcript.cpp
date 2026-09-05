// rolltui/Transcript.cpp — the SHIM over `rolltui/c/rolltui_transcript.h`: the RAII, the
// styling vocabulary, the eleven action names, and the translation of two `std::function`s
// into function pointers. The two implementations live in `TranscriptCpp.cpp` and
// `c/rolltui_transcript.c`, and this file is the C++ API over it (Phase 15 m5e).
//
// Nothing here decides anything. It exists so the C boundary never has to know what a `Role`
// is called, what an action is called, or what a `std::function` is.
#include "rolltui/Transcript.hpp"

#include "rolltui/Scratch.hpp"  // rolltui::ThreadHandle

namespace rolltui {

namespace {

// PHASE 17 m2a: the eleven are `rolltui_library_actions.c`'s now (one list, four expansions).
// What crosses instead of a `std::function`: the host's callable behind a `void*`.
void call_copy(void* ctx, const char* text, std::size_t len) {
  Transcript& t = *static_cast<Transcript*>(ctx);
  if (t.on_copy) t.on_copy(std::string(text, len));
}

RolltuiDrawScratch* draw_scratch() {
  static thread_local ThreadHandle<RolltuiDrawScratch, rolltui_draw_scratch_new, rolltui_draw_scratch_free> h;
  return h.get();
}

// The highlighting trampoline, and the two buffers the translation needs. Both belong to the
// `Transcript`, so a highlighted block costs no allocation after the first — the same shape
// `Markdown.cpp` uses one level down, because it is the same seam.
}  // namespace

// The highlighting trampoline's context, and the two buffers the translation needs. All three
// belong to the `Transcript`, so a highlighted block costs no allocation after the first — the
// same shape `Markdown.cpp` uses one level down, because it is the same seam.
struct TranscriptHighlightCtx {
  const markdown::Highlighter* fn;
  std::vector<std::string_view>* lines;
  std::vector<markdown::HighlightSpan>* spans;
};

namespace {
void call_highlighter(void* ctx, const char* lang, std::size_t lang_n, const RolltuiMdCodeLine* lines,
                      std::size_t line_count, std::size_t index, RolltuiMdSpanSink emit, void* sink) {
  TranscriptHighlightCtx& h = *static_cast<TranscriptHighlightCtx*>(ctx);
  h.lines->resize(line_count);
  for (std::size_t i = 0; i < line_count; ++i) (*h.lines)[i] = std::string_view(lines[i].p, lines[i].n);
  *h.spans = (*h.fn)(std::string_view(lang, lang_n), std::span<const std::string_view>(*h.lines), index);
  for (const markdown::HighlightSpan& s : *h.spans)
    emit(sink, s.begin, s.end, static_cast<unsigned char>(s.role));
}
}  // namespace

// THE SIX ROLES A DRAW NEEDS are `rolltui_transcript_new`'s own defaults as of Phase 17 m1c:
// this constructor and `transcript_test.cpp`'s handle were setting the identical table right
// after it — rule 5's tell — and the vocabulary they spelled is the C's own now
// (`rolltui_style.h`), so there was nothing left for this side to supply.
// `rolltui_transcript_set_roles` remains for a caller wanting different ones; nothing does.
Transcript::Transcript() { rolltui_transcript_set_copy(t_.get(), call_copy, this); }
Transcript::~Transcript() = default;

void Transcript::layout(const Document& doc, Rect area, const TranscriptOptions& opt) {
  rolltui_transcript_layout(t_.get(), &doc.entries, area, &opt);
}

void Transcript::draw(Frame& frame, const Theme& theme) const {
  rolltui_transcript_draw(t_.get(), frame.handle(), draw_scratch(), theme.styles.data());
}

// The handle-taking form, for the same reason `menu_handle` has one (Phase 17 m1c): a host
// driving the transcript the window table owns holds a `RolltuiTranscript*`, and the eleven
// action names are this file's — `rolltui_transcript.h` says they never cross by themselves.
bool transcript_handle(RolltuiTranscript* t, const Event& e, const Document& doc, std::uint64_t now_ms,
                       const Bindings& bindings) {
  if (std::holds_alternative<ResizeEvent>(e)) return false;
  const RolltuiEvent ev = c_event_of(e);
  return rolltui_transcript_handle(t, &ev, &doc.entries, now_ms, bindings.handle(), rolltui_transcript_default_actions()) != 0;
}

bool Transcript::handle(const Event& e, const Document& doc, std::uint64_t now_ms, const Bindings& bindings) {
  return transcript_handle(t_.get(), e, doc, now_ms, bindings);
}

bool Transcript::toggle_fold_nearest_top(const Document& doc) {
  return rolltui_transcript_toggle_fold_nearest_top(t_.get(), &doc.entries) != 0;
}

void Transcript::set_highlight(markdown::Highlighter h) {
  highlight_ = std::move(h);
  // The context is a member of THIS object, so it outlives every render the boundary makes
  // with it; unset is the seam's opt-in, and NULL means the renderer is never asked.
  if (highlight_) {
    // One context per transcript, allocated once and kept — the trampoline needs a stable
    // address, and a per-render one would be an allocation per entry per frame.
    hl_ctx_ = std::make_unique<TranscriptHighlightCtx>(
        TranscriptHighlightCtx{&highlight_, &hl_lines_, &hl_spans_});
    rolltui_transcript_set_highlight(t_.get(), call_highlighter, hl_ctx_.get());
  } else {
    rolltui_transcript_set_highlight(t_.get(), nullptr, nullptr);
  }
}

ScrollAnchor Transcript::scroll() const {
  ScrollAnchor a;
  rolltui_transcript_scroll(t_.get(), &a);
  return a;
}

std::string_view Transcript::query() const {
  std::size_t n = 0;
  const char* p = rolltui_transcript_query(t_.get(), &n);
  return std::string_view(p, n);
}

FindMatch Transcript::match_at(std::size_t i) const {
  FindMatch m;
  rolltui_transcript_match_at(t_.get(), i, &m);
  return m;
}

std::optional<FindMatch> Transcript::current_match() const {
  FindMatch m;
  if (!rolltui_transcript_current_match(t_.get(), &m)) return std::nullopt;
  return m;
}

Selection Transcript::selection() const {
  Selection s;
  rolltui_transcript_selection(t_.get(), &s);
  return s;
}

std::optional<TextPos> Transcript::hit(int x, int y) const {
  TextPos p;
  if (!rolltui_transcript_hit(t_.get(), x, y, &p)) return std::nullopt;
  return p;
}

std::string Transcript::selected_text() const {
  Str s;
  rolltui_transcript_selected_text(t_.get(), &s);
  return s.str();
}

bool Transcript::copy_selection() { return rolltui_transcript_copy_selection(t_.get()) != 0; }

TranscriptStats Transcript::stats() const {
  TranscriptStats s;
  rolltui_transcript_stats(t_.get(), &s);
  return s;
}

Rect Transcript::area() const {
  Rect r;
  rolltui_transcript_area(t_.get(), &r);
  return r;
}

Rect Transcript::text_area() const {
  Rect r;
  rolltui_transcript_text_area(t_.get(), &r);
  return r;
}

}  // namespace rolltui
