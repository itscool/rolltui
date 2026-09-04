// rolltui/TranscriptCpp.cpp — the C++ side of the transcript widget, behind the same
// boundary as `c/rolltui_transcript.c` (rolltui/c/rolltui_transcript.h, Phase 15 m5e). One
// CMake flag picks which of the two links; both satisfy `rolltui/tests/transcript_test.cpp`,
// the files-only proof and every golden frame.
//
// This is the code Phase 9 m9 wrote, Phase 12 m4/m5b grew find and code folding into, and
// Phase 15 m4 rewrote onto the span store — moved behind the boundary. It is deliberately NOT
// a transliteration of the C: it keeps `std::unordered_map` for the five caches, `std::vector`
// for the per-frame arrays, `std::optional` and the library's own C++ helpers (`WrapLines`,
// `unicode::graphemes_into`, `markdown::Document`), exactly as `MarkdownCpp.cpp` does.
#include "rolltui/c/rolltui_transcript.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rolltui/Effects.hpp"
#include "rolltui/Markdown.hpp"
#include "rolltui/Marker.hpp"
#include "rolltui/Scratch.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/c/rolltui_input.h"  // RolltuiCopyFn: one clipboard seam, not two
#include "rolltui/c/rolltui_layout.h"

namespace rolltui {
namespace {
std::string_view view(const RolltuiStr& s) { return std::string_view(s.p ? s.p : "", s.n); }
}  // namespace
}  // namespace rolltui

using rolltui::Frame;
using rolltui::Line;
using rolltui::Rect;
using rolltui::Role;
using rolltui::Style;
using rolltui::WrapGrapheme;
using rolltui::WrapLines;
using rolltui::WrapOptions;
using rolltui::EffectState;
using rolltui::Scratch;
using rolltui::scroll_marker_text;
using rolltui::view;
namespace markdown = rolltui::markdown;
namespace unicode = rolltui::unicode;
using markdown::kNoSource;
using markdown::Span;
using markdown::StyledLine;
using Color = RolltuiStyleColor;

// ---- the state ------------------------------------------------------------------------------
// Every member the C++ `Transcript` had, with the two `std::function`s replaced by a function
// pointer and a context (the boundary's rule 3) and `Document`/`DocEntry` now the C structs.

struct RolltuiTranscript {
  struct CacheKey {
    std::uint64_t version = 0;
    int width = 0;
    bool ambiguous = false;
    int tab = 8;
    bool folded = false;
    std::uint64_t code_epoch = 0;
    std::uint64_t highlight_epoch = 0;
    bool operator==(const CacheKey&) const = default;
  };
  struct Cached {
    CacheKey key;
    RolltuiEntryLayout layout;
    bool seen = false;
    ~Cached() { rolltui_entry_layout_release(&layout); }
    Cached() = default;
    Cached(Cached&& o) noexcept : key(o.key), layout(o.layout), seen(o.seen) { o.layout.store = nullptr; }
    Cached& operator=(Cached&& o) noexcept {
      if (this != &o) {
        rolltui_entry_layout_release(&layout);
        key = o.key;
        layout = o.layout;
        seen = o.seen;
        o.layout.store = nullptr;
      }
      return *this;
    }
    Cached(const Cached&) = delete;
    Cached& operator=(const Cached&) = delete;
  };
  struct RowRef {
    std::size_t entry = 0;
    std::size_t line = 0;
    bool gap = false;
    bool beyond = false;
  };
  struct EntryState {
    EffectState state = EffectState::None;
    double progress = 0;
    std::uint64_t since_ms = 0;
  };
  struct Parsed {
    std::uint64_t version = 0;
    markdown::Document doc;
    bool seen = false;
  };
  struct FindText {
    CacheKey key;
    std::string text;
  };
  struct CodeFolds {
    std::uint64_t epoch = 0;
    std::vector<markdown::CodeFoldState> states;
  };

  ~RolltuiTranscript() { rolltui_entry_layout_release(&unfolded_); }

  // ---- the mechanics, unchanged from the class they were members of ----
  void code_fold_for(const std::string& id, const RolltuiTranscriptOptions& opt,
                     std::vector<RolltuiMdFoldState>& out) const;
  const markdown::Document& parsed(const RolltuiDocEntry& e);
  void lay_out(RolltuiEntryLayout& L, const RolltuiDocEntry& e, int width,
               const RolltuiTranscriptOptions& opt, bool folded);
  std::size_t block_len(std::size_t entry) const;
  std::size_t max_top() const;
  void set_top(std::size_t top);
  std::size_t top_line() const;
  std::size_t lines_below() const;
  void build(const RolltuiDocument& doc, int width);
  void layout(const RolltuiDocument& doc, Rect area, const RolltuiTranscriptOptions& opt);
  bool set_query(std::string_view q);
  std::string_view searchable_text(const RolltuiDocEntry& e, std::size_t entry, int width);
  void recompute_matches(const RolltuiDocument& doc, int width);
  std::size_t line_of_offset(std::size_t entry, std::size_t offset) const;
  void reveal_current(const RolltuiDocument& doc, int width);
  bool find_next();
  bool find_prev();
  RowRef row_at(std::size_t global) const;
  const RolltuiEntryLayout* layout_of(std::size_t entry) const;
  void draw(RolltuiFrame* f, RolltuiDrawScratch* d, const RolltuiStyle* styles) const;
  void scroll_by(long lines);
  void scroll_page(int direction);
  void scroll_to_top();
  void scroll_to_bottom();
  bool is_folded(const RolltuiDocEntry& e) const;
  void set_folded(std::string_view id, bool folded) { fold_override_[std::string(id)] = folded; }
  void toggle_fold(const RolltuiDocEntry& e) { set_folded(view(e.id), !is_folded(e)); }
  void set_code_folded(std::string_view id, std::size_t block, bool folded);
  void set_code_uncapped(std::string_view id, std::size_t block, bool uncapped);
  const markdown::CodeBlockInfo* hiding_block(std::size_t entry, std::size_t offset) const;
  bool toggle_fold_nearest_top(const RolltuiDocument& doc);
  std::optional<RolltuiTextPos> hit_row(const RowRef& r, int x) const;
  std::optional<RolltuiTextPos> hit(int x, int y) const;
  std::string selected_text() const;
  bool copy_selection();
  void begin_drag(int x, int y, bool shift, std::uint64_t now_ms, const RolltuiDocument& doc);
  void drag_to(int x, int y);
  void end_drag();
  bool handle(const RolltuiEvent& e, const RolltuiDocument& doc, std::uint64_t now_ms,
              const RolltuiBindings* bindings, const RolltuiTranscriptActions& A);
  void tick();
  bool wants_tick() const { return drag_.active && drag_.outside; }
  void clear_selection() { sel_ = {}; }
  const RolltuiFindMatch* current_match() const {
    return current_ && *current_ < matches_.size() ? &matches_[*current_] : nullptr;
  }

  std::unordered_map<std::string, Parsed> parse_;
  std::string pad_;
  std::string scratch_;
  RolltuiEntryLayout unfolded_{};
  std::unordered_map<std::string, Cached> cache_;
  std::vector<const RolltuiEntryLayout*> layouts_;
  std::vector<EntryState> states_;
  std::vector<std::size_t> starts_;
  std::size_t total_ = 0;
  Rect area_, text_area_;
  RolltuiTranscriptOptions opt_;
  RolltuiScrollAnchor scroll_;
  RolltuiSelection sel_;
  std::unordered_map<std::string, bool> fold_override_;
  std::unordered_map<std::string, CodeFolds> code_folds_;
  RolltuiMdHighlightFn highlight_ = nullptr;
  void* highlight_ctx_ = nullptr;
  std::uint64_t highlight_epoch_ = 0;
  std::string query_;
  std::vector<RolltuiFindMatch> matches_;
  std::optional<std::size_t> current_;
  bool find_dirty_ = false;
  bool reveal_ = false;
  std::unordered_map<std::string, FindText> find_text_;
  struct Drag {
    bool active = false, outside = false;
    int x = 0, y = 0;
    std::size_t origin_entry = 0, origin_begin = 0, origin_end = 0;
  } drag_;
  struct Click {
    std::uint64_t at_ms = 0;
    int x = -1, y = -1, count = 0;
  } click_;
  RolltuiTranscriptRoles roles_{};
  RolltuiTranscriptStats stats_;
  RolltuiCopyFn copy_fn_ = nullptr;
  void* copy_ctx_ = nullptr;

  static void unit_around(std::string_view text, std::size_t off, bool word, std::size_t& b,
                          std::size_t& en);
};

// ---- selection model -------------------------------------------------------------

// Entry, then offset, then length — so of two positions at the same offset the one
// that covers a grapheme is `last()`, and range_in includes it.
extern "C" int rolltui_text_pos_less(const RolltuiTextPos* pa, const RolltuiTextPos* pb) {
  const RolltuiTextPos& a = *pa;
  const RolltuiTextPos& b = *pb;
  if (a.entry != b.entry) return a.entry < b.entry;
  if (a.offset != b.offset) return a.offset < b.offset;
  return a.length < b.length;
}

extern "C" int rolltui_selection_range_in(const RolltuiSelection* sel, std::size_t entry, std::size_t len,
                                          std::size_t* out_begin, std::size_t* out_end) {
  if (!sel->active) return 0;
  const RolltuiTextPos f = sel->first(), l = sel->last();
  if (entry < f.entry || entry > l.entry) return 0;
  std::size_t begin = (entry == f.entry) ? std::min(f.offset, len) : 0;
  std::size_t end = (entry == l.entry) ? std::min(l.offset + l.length, len) : len;
  if (end < begin) end = begin;
  *out_begin = begin;
  *out_end = end;
  return 1;
}

bool RolltuiSelection::range_in(std::size_t entry, std::size_t len, std::size_t& begin,
                                std::size_t& end) const {
  return rolltui_selection_range_in(this, entry, len, &begin, &end) != 0;
}

namespace {

// A highlight applied over a base style: a colour only where the highlight names one,
// attributes OR'd in. ONE function, so the selection and the two find highlights cannot
// drift apart in how they combine with the text's own role — which is exactly what
// happened the first time this was written twice.
Style overlay_style(Style base, const Style& over) {
  if (over.fg.kind != Color::Kind::None) base.fg = over.fg;
  if (over.bg.kind != Color::Kind::None) base.bg = over.bg;
  base.bold |= over.bold;
  base.italic |= over.italic;
  base.underline |= over.underline;
  base.dim |= over.dim;
  base.reverse |= over.reverse;
  return base;
}

// Visits every drawable grapheme of a line: the span, the grapheme's index within the
// span (its Span::sources index), its bytes and its width. Width-0 clusters are
// skipped exactly as Frame::put_text skips them, so cell positions agree.
template <typename F>
void for_each_cell(const StyledLine& line, bool ambiguous, F&& f) {
  // Phase 13 m3: one reused buffer per instantiation. This runs once per SPAN of every
  // drawn row, twice per row in draw() — the second heaviest caller of `graphemes()` after
  // put_text. Being a template is what makes it safe without thought: each lambda type
  // gets its own buffer, and no call site's lambda calls back into for_each_cell.
  static thread_local Scratch<std::vector<unicode::Grapheme>> scratch("for_each_cell clusters");
  auto gs = scratch.lock();
  for (const Span& sp : line.spans()) {
    std::size_t k = 0;
    unicode::graphemes_into(sp.text(), ambiguous, *gs);
    for (const unicode::Grapheme& g : *gs) {
      if (g.width > 0) f(sp, k, sp.text().substr(g.offset, g.length), g.width);
      ++k;
    }
  }
}

std::uint32_t source_of(const Span& sp, std::size_t k) {
  const std::span<const std::uint32_t> src = sp.sources();
  if (src.empty()) return kNoSource;
  return src[std::min(k, src.size() - 1)];
}

// A chrome span of the open line: no source offsets and no href, so every one of its
// clusters is kNoSource — generated by the store rather than passed, which is what
// `chrome()` used to build a whole `std::vector` to say.
std::string_view spaces(std::string& pad, int n) {
  const std::size_t need = static_cast<std::size_t>(std::max(n, 0));
  if (pad.size() < need) pad.resize(need, ' ');
  return std::string_view(pad).substr(0, need);
}

void chrome(RolltuiMdLines* S, std::string_view text, unsigned char role, bool ambiguous) {
  rolltui_md_lines_span(S, text.data(), text.size(), role, ambiguous, nullptr, 0, nullptr, 0);
}

}  // namespace

// ---- layout ----------------------------------------------------------------------

// FILLS A CALLER'S VECTOR rather than returning one: this runs per relaid entry per frame,
// and the states are a handful of rows the caller already has storage for.
void RolltuiTranscript::code_fold_for(const std::string& id, const RolltuiTranscriptOptions&,
                                      std::vector<RolltuiMdFoldState>& out) const {
  out.clear();
  auto it = code_folds_.find(id);
  if (it == code_folds_.end()) return;
  for (const markdown::CodeFoldState& s : it->second.states)
    out.push_back({s.index, static_cast<unsigned char>(s.folded), static_cast<unsigned char>(s.uncapped)});
}

const markdown::Document& RolltuiTranscript::parsed(const RolltuiDocEntry& e) {
  // KEYED ON VERSION AND NOT ON WIDTH, which is the whole finding. `parse()` is a pure
  // function of the entry's text, and the layout cache's key carries `width` — so a resize
  // re-parsed forty unchanged strings into an identical tree and threw it away, 1,243
  // allocations a frame (m1, 12.1%). The port only SURFACED this: C has no opinion about
  // cache keys. It is fixed here anyway rather than left as a 12% cost the port walked past.
  auto it = parse_.find(std::string(view(e.id)));
  if (it == parse_.end() || it->second.version != e.version) {
    Parsed p;
    p.version = e.version;
    p.doc = markdown::parse(e.text);
    it = parse_.insert_or_assign(std::string(view(e.id)), std::move(p)).first;
  }
  it->second.seen = true;
  return it->second.doc;
}

void RolltuiTranscript::lay_out(RolltuiEntryLayout& L, const RolltuiDocEntry& e, int width, const RolltuiTranscriptOptions& opt, bool folded) {
  RolltuiMdLines* S = rolltui_entry_layout_store(&L);
  L.folded = static_cast<unsigned char>(folded && e.foldable);
  L.hidden_lines = 0;
  const bool amb = opt.ambiguous_wide;
  const int prefix_w = unicode::display_width(view(e.prefix), amb);
  const int inner = std::max(width - prefix_w, 1);

  // The BODY, laid out into the front of the store. Everything below appends after it and
  // references its spans; nothing copies a byte of it.
  if (e.markdown) {
    // The C render options directly, so the host's highlighter travels as the function
    // pointer it already is — one seam and not two (the boundary's rule 3).
    std::vector<RolltuiMdFoldState> states;
    code_fold_for(std::string(view(e.id)), opt, states);
    RolltuiMdRenderOptions ro{};
    ro.width = inner;
    ro.ambiguous_wide = amb;
    ro.tab_width = opt.tab_width;
    ro.base = static_cast<unsigned char>(e.role);
    ro.roles = *markdown::md_roles();
    ro.highlight = highlight_;
    ro.highlight_ctx = highlight_ctx_;
    ro.fold_over_lines = opt.code_fold_over_lines;
    ro.cap_lines = opt.code_cap_lines;
    ro.states = states.empty() ? nullptr : states.data();
    ro.state_count = states.size();
    rolltui_md_render(S, parsed(e).handle(), &ro);
  } else {
    rolltui_md_lines_reset(S);
    rolltui_md_lines_text_set(S, e.text.p, e.text.n);
    WrapOptions wo;
    wo.ambiguous_wide = amb;
    wo.tab_width = opt.tab_width;
    // LENT: the one wrap engine this widget reuses for every plain entry of every frame.
    static thread_local Scratch<WrapLines> wrapper("transcript plain wrap");
    auto lines = wrapper.lock();
    lines->wrap(view(e.text), inner, wo);
    static thread_local Scratch<std::vector<std::uint32_t>> sources("transcript plain sources");
    for (const Line& l : *lines) {
      rolltui_md_lines_open(S);
      if (l.indent > 0) chrome(S, spaces(pad_, l.indent), static_cast<unsigned char>(e.role), amb);
      auto srcs = sources.lock();
      for (const WrapGrapheme& g : l.graphemes) srcs->push_back(static_cast<std::uint32_t>(g.source_offset));
      rolltui_md_lines_span(S, l.text.data(), l.text.size(), static_cast<unsigned char>(e.role), amb, srcs->data(),
                            srcs->size(), nullptr, 0);
      rolltui_md_lines_close(S);
    }
  }
  std::size_t body = rolltui_md_lines_count(S);
  if (body == 0) {  // an entry with nothing in it still occupies one line
    rolltui_md_lines_open(S);
    rolltui_md_lines_close(S);
    body = 1;
  }
  L.body = body;

  auto with_prefix = [&](bool first) {
    if (prefix_w <= 0) return;
    if (first) chrome(S, view(e.prefix), static_cast<unsigned char>(e.prefix_role), amb);
    else chrome(S, spaces(pad_, prefix_w), static_cast<unsigned char>(e.role), amb);
  };
  auto add_body = [&](std::size_t line, bool first) {
    rolltui_md_lines_open(S);
    with_prefix(first);
    std::size_t f = 0, n = 0;
    rolltui_md_lines_span_range(S, line, &f, &n);
    // BY INDEX, never by pointer: `with_prefix` above may have grown the pools, and the
    // refs below grow the span array under anything the caller took a pointer to.
    for (std::size_t k = 0; k < n; ++k) rolltui_md_lines_span_ref(S, f + k);
    rolltui_md_lines_close(S);
  };

  if (e.foldable) {
    rolltui_md_lines_open(S);
    with_prefix(true);
    chrome(S, L.folded ? "\xE2\x96\xB8 " : "\xE2\x96\xBE ", roles_.text_muted, amb);  // ▸ ▾
    chrome(S, view(e.summary), static_cast<unsigned char>(e.role), amb);
    if (L.folded) {
      scratch_.assign(" (");
      scratch_ += std::to_string(body);
      scratch_ += body == 1 ? " line)" : " lines)";
      chrome(S, scratch_, roles_.text_muted, amb);
    }
    rolltui_md_lines_close(S);
    if (L.folded) {
      L.hidden_lines = body;
      rolltui_md_lines_text_set(S, e.summary.p, e.summary.n);
      rolltui_md_lines_clear_code_blocks(S);  // nothing of the body is drawn, so nothing is clickable
      rolltui_md_lines_finish(S);
      return;
    }
    // The entry's own summary row pushes every body line down by one, so the block rows
    // have to move with it — a click routes by LINE NUMBER, and an off-by-one here is a
    // header row that toggles nothing.
    rolltui_md_lines_shift_code_blocks(S, 1);
    for (std::size_t k = 0; k < body; ++k) add_body(k, false);
  } else {
    for (std::size_t k = 0; k < body; ++k) add_body(k, k == 0);
  }
  rolltui_md_lines_finish(S);
}

std::size_t RolltuiTranscript::block_len(std::size_t entry) const {
  return (entry > 0 ? static_cast<std::size_t>(std::max(opt_.gap, 0)) : 0) + layouts_[entry]->lines().size();
}

std::size_t RolltuiTranscript::max_top() const {
  const std::size_t h = static_cast<std::size_t>(std::max(area_.h, 0));
  return total_ > h ? total_ - h : 0;
}

void RolltuiTranscript::set_top(std::size_t top) {
  if (starts_.empty()) { scroll_ = {0, 0, true}; return; }
  top = std::min(top, max_top());
  auto it = std::upper_bound(starts_.begin(), starts_.end(), top);
  std::size_t e = static_cast<std::size_t>(it - starts_.begin()) - 1;
  scroll_.entry = e;
  scroll_.line = top - starts_[e];
  scroll_.follow = top >= max_top();
}

std::size_t RolltuiTranscript::top_line() const {
  if (starts_.empty()) return 0;
  std::size_t e = std::min(scroll_.entry, starts_.size() - 1);
  return starts_[e] + scroll_.line;
}

std::size_t RolltuiTranscript::lines_below() const {
  const std::size_t shown_end = top_line() + static_cast<std::size_t>(std::max(area_.h, 0));
  return total_ > shown_end ? total_ - shown_end : 0;
}

void RolltuiTranscript::build(const RolltuiDocument& doc, int width) {
  const std::size_t n = doc.size();
  layouts_.assign(n, nullptr);
  starts_.assign(n, 0);
  // Phase 12 m6. An entry's STATE is copied here, not into the cache key: it changes the
  // marks draw() emits and never a single line, so a turn going from waiting to streaming
  // must not re-wrap the transcript.
  states_.assign(n, EntryState{});
  for (auto& [id, c] : cache_) c.seen = false;
  std::size_t g = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const RolltuiDocEntry& e = doc[i];
    const bool folded = e.foldable != 0 && is_folded(e);
    std::uint64_t code_epoch = 0;
    if (auto cf = code_folds_.find(std::string(view(e.id))); cf != code_folds_.end()) code_epoch = cf->second.epoch;
    const CacheKey key{e.version, width, opt_.ambiguous_wide != 0, opt_.tab_width, folded, code_epoch, highlight_epoch_};
    auto it = cache_.find(std::string(view(e.id)));
    if (it == cache_.end() || !(it->second.key == key)) {
      // The entry is laid out INTO the cache's own EntryLayout, so a re-lay reuses every
      // buffer it already had: a resize refills the pools instead of rebuilding them.
      if (it == cache_.end()) it = cache_.try_emplace(std::string(view(e.id))).first;
      it->second.key = key;
      lay_out(it->second.layout, e, width, opt_, folded);
      ++stats_.entries_relaid;
    }
    it->second.seen = true;
    states_[i] = {e.state, e.progress, e.state_since_ms};
    layouts_[i] = &it->second.layout;
    starts_[i] = g;
    g += block_len(i);
  }
  total_ = g;
  if (cache_.size() > 2 * n + 32) {
    for (auto it = cache_.begin(); it != cache_.end();) {
      if (!it->second.seen) it = cache_.erase(it);
      else ++it;
    }
  }
  // Reconcile the anchor with the new layout.
  if (n == 0) {
    scroll_ = {0, 0, true};
  } else if (scroll_.follow) {
    set_top(max_top());
  } else {
    scroll_.entry = std::min(scroll_.entry, n - 1);
    const std::size_t len = block_len(scroll_.entry);
    scroll_.line = std::min(scroll_.line, len > 0 ? len - 1 : 0);
    set_top(starts_[scroll_.entry] + scroll_.line);
  }
}

void RolltuiTranscript::layout(const RolltuiDocument& doc, Rect area, const RolltuiTranscriptOptions& opt) {
  const auto t0 = std::chrono::steady_clock::now();
  area_ = area;
  opt_ = opt;
  text_area_ = area;
  if (opt.inset > 0 && area.w >= 2 * opt.inset + 1) {
    text_area_.x += opt.inset;
    text_area_.w -= 2 * opt.inset;
  }
  const int width = std::max(text_area_.w, 1);
  stats_.entries_relaid = 0;
  build(doc, width);
  // A relaid entry means text moved under the match list — a streaming answer, a
  // re-wrap, a fold toggle — so offsets recorded against the old text are stale. Cheap
  // to notice here; expensive to debug as a highlight drawn over the wrong bytes.
  if (!query_.empty() && stats_.entries_relaid > 0) find_dirty_ = true;
  // Find runs AFTER the build (it needs the layouts to place a match on a line) and can
  // change the layout by unfolding, which is why reveal_current re-runs build().
  if (find_dirty_) {
    recompute_matches(doc, width);
    find_dirty_ = false;
  }
  if (reveal_) {
    reveal_current(doc, width);
    reveal_ = false;
  }
  stats_.total_lines = total_;
  stats_.cache_size = cache_.size();
  stats_.layout_us = static_cast<long>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
}

// ---- find (see FIND in Transcript.hpp) -------------------------------------------

namespace {

// ASCII-case-insensitive, non-overlapping, left to right. Stated in the header rather
// than inferred: this library has no Unicode case folding, and folding only the scripts
// we happen to have tables for would be a rule nobody could predict.
char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool matches_at(std::string_view hay, std::size_t at, const std::string& needle) {
  if (at + needle.size() > hay.size()) return false;
  for (std::size_t k = 0; k < needle.size(); ++k)
    if (lower_ascii(hay[at + k]) != lower_ascii(needle[k])) return false;
  return true;
}

}  // namespace

bool RolltuiTranscript::set_query(std::string_view q) {
  if (query_ == q) return false;
  query_.assign(q);
  matches_.clear();
  current_.reset();
  find_dirty_ = true;
  // An empty query clears and NEVER moves the view — the one movement rule the
  // milestone names, because a find bar you just emptied must not throw you somewhere.
  reveal_ = !query_.empty();
  return true;
}

std::string_view RolltuiTranscript::searchable_text(const RolltuiDocEntry& e, std::size_t entry, int width) {
  // For everything but a FOLDED entry the drawn layout's text already is the unfolded
  // logical text, so the common case costs nothing.
  // A code fold does NOT need a variant here: it hides lines and never text
  // (Markdown.hpp), so the drawn layout's text already holds every byte of every block.
  if (!(e.foldable != 0 && is_folded(e))) return layouts_[entry]->text();
  std::uint64_t code_epoch = 0;
  if (auto cf = code_folds_.find(std::string(view(e.id))); cf != code_folds_.end()) code_epoch = cf->second.epoch;
  const CacheKey key{e.version, width, opt_.ambiguous_wide != 0, opt_.tab_width, /*folded=*/false, code_epoch,
                     highlight_epoch_};
  auto it = find_text_.find(std::string(view(e.id)));
  if (it == find_text_.end() || !(it->second.key == key)) {
    FindText ft;
    ft.key = key;
    lay_out(unfolded_, e, width, opt_, /*folded=*/false);
    ft.text.assign(unfolded_.text());
    it = find_text_.insert_or_assign(std::string(view(e.id)), std::move(ft)).first;
  }
  return it->second.text;
}

void RolltuiTranscript::recompute_matches(const RolltuiDocument& doc, int width) {
  // Where the user WAS, so a recompute forced by a streaming answer does not silently
  // move them back to the first match while they are stepping through.
  const std::optional<RolltuiFindMatch> was = current_match() ? std::optional<RolltuiFindMatch>(*current_match()) : std::nullopt;
  matches_.clear();
  current_.reset();
  if (query_.empty()) return;
  for (std::size_t i = 0; i < doc.size(); ++i) {
    const std::string_view hay = searchable_text(doc[i], i, width);
    std::size_t at = 0;
    while (at + query_.size() <= hay.size()) {
      if (matches_at(hay, at, query_)) {
        matches_.push_back({i, at, query_.size()});
        at += std::max<std::size_t>(query_.size(), 1);
      } else {
        ++at;
      }
    }
  }
  if (matches_.empty()) return;
  if (was) {
    for (std::size_t k = 0; k < matches_.size(); ++k)
      if (matches_[k] == *was) { current_ = k; return; }
  }
  // The first match at or after the top of the view, so typing into a find bar moves
  // forward from where you are rather than jumping to the top of the document.
  const std::size_t top = top_line();
  std::size_t pick = 0;
  for (std::size_t k = 0; k < matches_.size(); ++k) {
    if (matches_[k].entry < starts_.size() && starts_[matches_[k].entry] + block_len(matches_[k].entry) > top) { pick = k; break; }
  }
  current_ = pick;
}

std::size_t RolltuiTranscript::line_of_offset(std::size_t entry, std::size_t offset) const {
  const RolltuiEntryLayout* L = layout_of(entry);
  if (!L || L->lines().empty()) return 0;
  std::size_t best = 0;
  for (std::size_t i = 0; i < L->lines().size(); ++i) {
    bool any = false;
    std::size_t lo = 0, hi = 0;
    for_each_cell(L->lines()[i], opt_.ambiguous_wide, [&](const Span& sp, std::size_t k, std::string_view gt, int) {
      const std::uint32_t src = source_of(sp, k);
      if (src == kNoSource) return;
      if (!any) { lo = src; hi = src + gt.size(); any = true; }
      else { lo = std::min<std::size_t>(lo, src); hi = std::max<std::size_t>(hi, src + gt.size()); }
    });
    if (!any) continue;
    if (offset < hi) return i;
    if (lo <= offset) best = i;
  }
  return best;
}

void RolltuiTranscript::reveal_current(const RolltuiDocument& doc, int width) {
  const RolltuiFindMatch* m = current_match();
  if (!m || m->entry >= doc.size()) return;
  // A match inside a folded block: unfold it, then re-run the build, because every line
  // number below the entry has just moved.
  const RolltuiDocEntry& e = doc[m->entry];
  if (e.foldable != 0 && is_folded(e)) {
    set_folded(view(e.id), false);
    build(doc, width);
  }
  // …and the same one rung down: a match inside a folded or capped CODE BLOCK has no
  // drawn line to scroll to, so open the block and rebuild. This is the price of the
  // rule that makes the count stable — the text was always there, the lines were not.
  if (const markdown::CodeBlockInfo* b = hiding_block(m->entry, m->offset)) {
    set_code_folded(view(e.id), b->index, false);
    set_code_uncapped(view(e.id), b->index, true);
    build(doc, width);
  }
  const std::size_t line = line_of_offset(m->entry, m->offset);
  const std::size_t gapn = m->entry > 0 ? static_cast<std::size_t>(std::max(opt_.gap, 0)) : 0;
  const std::size_t g = starts_[m->entry] + gapn + line;
  const std::size_t h = static_cast<std::size_t>(std::max(area_.h, 1));
  const std::size_t top = top_line();
  // Minimal movement: already in view, nothing moves. Deterministic, and it keeps a
  // find_next() within one screen from repainting the whole transcript.
  if (g < top) set_top(g);
  else if (g >= top + h) set_top(g - h + 1);
}

bool RolltuiTranscript::find_next() {
  if (matches_.empty()) return false;
  current_ = current_ ? (*current_ + 1) % matches_.size() : 0;
  reveal_ = true;
  return true;
}

bool RolltuiTranscript::find_prev() {
  if (matches_.empty()) return false;
  current_ = current_ && *current_ > 0 ? *current_ - 1 : matches_.size() - 1;
  reveal_ = true;
  return true;
}

RolltuiTranscript::RowRef RolltuiTranscript::row_at(std::size_t global) const {
  RowRef r;
  if (starts_.empty() || global >= total_) { r.beyond = true; r.entry = starts_.empty() ? 0 : starts_.size() - 1; return r; }
  auto it = std::upper_bound(starts_.begin(), starts_.end(), global);
  r.entry = static_cast<std::size_t>(it - starts_.begin()) - 1;
  const std::size_t local = global - starts_[r.entry];
  const std::size_t gapn = r.entry > 0 ? static_cast<std::size_t>(std::max(opt_.gap, 0)) : 0;
  if (local < gapn) { r.gap = true; return r; }
  r.line = local - gapn;
  return r;
}

const RolltuiEntryLayout* RolltuiTranscript::layout_of(std::size_t entry) const {
  return entry < layouts_.size() ? layouts_[entry] : nullptr;
}

// ---- drawing ---------------------------------------------------------------------

void RolltuiTranscript::draw(RolltuiFrame* f, RolltuiDrawScratch* d, const RolltuiStyle* styles) const {
  const RolltuiTranscriptRoles& roles = roles_;
  const Rect bounds{0, 0, rolltui_frame_width(f), rolltui_frame_height(f)};
  const Rect area = area_.intersect(bounds);
  if (area.empty()) return;
  rolltui_frame_fill(f, d, area, styles[roles.background], nullptr, 0);
  const bool amb = opt_.ambiguous_wide;
  const Style& sel_style = styles[roles.selection];
  auto styled = [&](unsigned char role, bool selected) {
    const Style s = styles[role];
    return selected ? overlay_style(s, sel_style) : s;
  };
  // A find highlight is the SAME range test the selection uses, on the same per-cell
  // source offsets — which is why a match that wraps lights up on both rows without
  // anything here knowing what a row is.
  const Style& match_style = styles[roles.find_match];
  const Style& current_style = styles[roles.find_current];
  const RolltuiFindMatch* cur = current_match();
  const std::size_t top = top_line();
  const int right = text_area_.x + text_area_.w;
  for (int row = 0; row < area_.h; ++row) {
    const std::size_t g = top + static_cast<std::size_t>(row);
    if (g >= total_) break;
    const RowRef r = row_at(g);
    if (r.gap || r.beyond) continue;
    const int y = area_.y + row;
    if (y < 0 || y >= bounds.h) continue;
    const StyledLine& line = layouts_[r.entry]->lines()[r.line];
    // Selection state of this line: the byte range within the entry, and whether the
    // line's text lies wholly inside it (then its chrome highlights too).
    std::size_t sb = 0, se = 0;
    const std::size_t len = layouts_[r.entry]->text().size();
    const bool in_sel = sel_.range_in(r.entry, len, sb, se);
    bool fully = false;
    if (in_sel) {
      bool has_text = false;
      std::size_t lo = 0, hi = 0;
      for_each_cell(line, amb, [&](const Span& sp, std::size_t k, std::string_view gt, int) {
        std::uint32_t src = source_of(sp, k);
        if (src == kNoSource) return;
        if (!has_text) { lo = src; hi = src + gt.size(); has_text = true; }
        else { lo = std::min<std::size_t>(lo, src); hi = std::max<std::size_t>(hi, src + gt.size()); }
      });
      fully = has_text ? (lo >= sb && hi <= se) : (sb == 0 && se >= len);
    }
    // This entry's matches only. matches_ is sorted by (entry, offset), so this is a
    // binary search rather than a scan of every match for every cell — the difference
    // between a find on a long transcript costing nothing and costing the frame.
    const RolltuiFindMatch* mb = matches_.data();
    const RolltuiFindMatch* me = mb;
    if (!matches_.empty()) {
      auto by_entry = [](const RolltuiFindMatch& m, std::size_t e) { return m.entry < e; };
      auto entry_by = [](std::size_t e, const RolltuiFindMatch& m) { return e < m.entry; };
      mb = std::lower_bound(matches_.begin(), matches_.end(), r.entry, by_entry).base();
      me = std::upper_bound(matches_.begin(), matches_.end(), r.entry, entry_by).base();
    }
    int x = text_area_.x;
    const int row_start = x;
    bool stop = false;
    for_each_cell(line, amb, [&](const Span& sp, std::size_t k, std::string_view gt, int w) {
      if (stop || x + w > right) { stop = true; return; }
      const std::uint32_t src = source_of(sp, k);
      const bool selected = fully || (in_sel && src != kNoSource && src >= sb && src < se);
      Style st = styled(sp.role, selected);
      // The selection WINS where they overlap (Transcript.hpp's FIND): it is the user's
      // most recent direct act. Otherwise the current match beats the other matches.
      if (!selected && src != kNoSource) {
        for (const RolltuiFindMatch* m = mb; m != me; ++m) {
          if (src >= m->offset && src < m->offset + m->length) {
            st = overlay_style(st, (cur && *m == *cur) ? current_style : match_style);
            break;
          }
        }
      }
      const std::string_view href = sp.href();
      const std::uint32_t link = sp.href_n == 0 ? 0 : rolltui_frame_link_id(f, href.data(), href.size());
      x += rolltui_frame_put(f, x, y, gt.data(), gt.size(), w, st, link);
    });
    // Phase 12 m6: this widget's ENTIRE contribution to motion. It marks the cells it just
    // drew with the entry's state and stops — no glyph, no colour, no clock of its own.
    // One mark per DRAWN ROW, so a state on a wrapped entry animates along each row rather
    // than across a rectangle that has no text in half of it.
    const EntryState& es = states_[r.entry];
    if (es.state != EffectState::None && x > row_start)
      rolltui_frame_mark(f, row_start, y, x - row_start, static_cast<int>(es.state), es.since_ms, es.progress);
  }
  const std::string marker = area_.h > 0 ? scroll_marker_text(lines_below(), text_area_.w, amb) : std::string();
  if (!marker.empty()) {
    const int mw = unicode::display_width(marker, amb);
    rolltui_frame_put_text(f, d, right - mw, area_.y + area_.h - 1, marker.data(), marker.size(),
                           styles[roles.scroll_marker], mw, amb, 0);
  }
}

// ---- scrolling -------------------------------------------------------------------

void RolltuiTranscript::scroll_by(long lines) {
  long t = static_cast<long>(top_line()) + lines;
  if (t < 0) t = 0;
  set_top(static_cast<std::size_t>(t));
}

void RolltuiTranscript::scroll_page(int direction) {
  const long page = std::max(area_.h - 1, 1);
  scroll_by(direction < 0 ? -page : page);
}

void RolltuiTranscript::scroll_to_top() { set_top(0); }

void RolltuiTranscript::scroll_to_bottom() {
  set_top(max_top());
  scroll_.follow = true;
}

// ---- folding ---------------------------------------------------------------------

bool RolltuiTranscript::is_folded(const RolltuiDocEntry& e) const {
  auto it = fold_override_.find(std::string(view(e.id)));
  return it != fold_override_.end() ? it->second : e.folded != 0;
}

// THE FIRST TOGGLE OF A BLOCK MUST BUMP THE EPOCH EVEN WHEN THE VALUE "MATCHES".
// A block that is folded by the THRESHOLD has no state row, so a default-constructed
// row reads folded=false — and an early return comparing against it left the row added,
// the block unfolded and the layout cache never invalidated: correct state, stale frame.
// Written out per field rather than through a shared helper so the create-and-bump case
// is visible at both call sites; opening a block does NOT lift its cap (the milestone's
// stated behaviour: an opened block is still capped, and the marker is what lifts it).
void RolltuiTranscript::set_code_folded(std::string_view id, std::size_t block, bool folded) {
  CodeFolds& f = code_folds_[std::string(id)];
  for (markdown::CodeFoldState& s : f.states) {
    if (s.index != block) continue;
    if (s.folded == folded) return;
    s.folded = folded;
    ++f.epoch;
    return;
  }
  f.states.push_back({block, folded, false});
  ++f.epoch;
}

void RolltuiTranscript::set_code_uncapped(std::string_view id, std::size_t block, bool uncapped) {
  CodeFolds& f = code_folds_[std::string(id)];
  for (markdown::CodeFoldState& s : f.states) {
    if (s.index != block) continue;
    if (s.uncapped == uncapped) return;
    s.uncapped = uncapped;
    ++f.epoch;
    return;
  }
  f.states.push_back({block, false, uncapped});
  ++f.epoch;
}

extern "C" void rolltui_transcript_set_highlight(RolltuiTranscript* t, RolltuiMdHighlightFn fn, void* ctx) {
  t->highlight_ = fn;
  t->highlight_ctx_ = ctx;
  // Bumped so a LATER highlighter re-lays everything instead of being silently ignored:
  // set-once is a host convention, not a guarantee, and a second call quietly dropped is the
  // shape of bug this project keeps finding.
  ++t->highlight_epoch_;
}

const markdown::CodeBlockInfo* RolltuiTranscript::hiding_block(std::size_t entry, std::size_t offset) const {
  const RolltuiEntryLayout* L = layout_of(entry);
  if (!L) return nullptr;
  for (const markdown::CodeBlockInfo& b : L->code_blocks()) {
    if (offset < b.text_begin || offset >= b.text_end) continue;
    if (b.folded) return &b;
    if (b.hidden == 0) return nullptr;
    // Capped: the first (lines - hidden) lines are drawn. Which line the offset is on is
    // a count of newlines from the block's start — the block's text is its lines, each
    // terminated, so this is exact rather than a search through the drawn spans.
    std::size_t line = 0;
    const std::string_view ltext = L->text();
    for (std::size_t i = b.text_begin; i < offset && i < ltext.size(); ++i)
      if (ltext[i] == '\n') ++line;
    return line >= b.lines - b.hidden ? &b : nullptr;
  }
  return nullptr;
}

bool RolltuiTranscript::toggle_fold_nearest_top(const RolltuiDocument& doc) {
  const std::size_t top = top_line();
  for (int row = 0; row < area_.h; ++row) {
    const std::size_t g = top + static_cast<std::size_t>(row);
    if (g >= total_) break;
    const RowRef r = row_at(g);
    if (r.gap || r.beyond || r.entry >= doc.size()) continue;
    // An entry's summary row is line 0; a code block's header row is anywhere. Whichever
    // is nearer the top wins, which is what "the first visible fold" has always meant.
    if (r.line == 0 && doc[r.entry].foldable) {
      toggle_fold(doc[r.entry]);
      return true;
    }
    for (const markdown::CodeBlockInfo& b : layouts_[r.entry]->code_blocks()) {
      if (b.header_line != r.line) continue;
      set_code_folded(view(doc[r.entry].id), b.index, !b.folded);
      return true;
    }
  }
  return false;
}

// ---- selection -------------------------------------------------------------------

std::optional<RolltuiTextPos> RolltuiTranscript::hit_row(const RowRef& r, int x) const {
  const StyledLine& line = layouts_[r.entry]->lines()[r.line];
  const bool amb = opt_.ambiguous_wide;
  std::optional<RolltuiTextPos> at, before, after, last;
  int cx = text_area_.x;
  for_each_cell(line, amb, [&](const Span& sp, std::size_t k, std::string_view gt, int w) {
    const std::uint32_t src = source_of(sp, k);
    const bool contains = x >= cx && x < cx + w;
    if (src != kNoSource) {
      RolltuiTextPos p{r.entry, src, gt.size()};
      last = p;
      if (contains) at = p;
      else if (cx + w <= x) before = RolltuiTextPos{r.entry, src + gt.size(), 0};
      else if (!after) after = RolltuiTextPos{r.entry, src, 0};
    }
    cx += w;
  });
  if (at) return at;
  if (x >= cx) {          // past the end of the line: the last grapheme, inclusive
    if (last) return last;
  } else {                // on chrome or before the first cell: the nearest text
    if (before) return before;
    if (after) return after;
  }
  // A line with no text at all (a summary line, a box rule): the end of the nearest
  // text above it in the same entry, else the entry's start.
  for (std::size_t li = r.line; li-- > 0;) {
    std::optional<RolltuiTextPos> found;
    for_each_cell(layouts_[r.entry]->lines()[li], amb, [&](const Span& sp, std::size_t k, std::string_view gt, int) {
      const std::uint32_t src = source_of(sp, k);
      if (src != kNoSource) found = RolltuiTextPos{r.entry, src + gt.size(), 0};
    });
    if (found) return found;
  }
  return RolltuiTextPos{r.entry, 0, 0};
}

std::optional<RolltuiTextPos> RolltuiTranscript::hit(int x, int y) const {
  if (layouts_.empty() || y < area_.y) return std::nullopt;
  int row = y - area_.y;
  if (row >= area_.h) row = std::max(area_.h - 1, 0);
  const std::size_t g = top_line() + static_cast<std::size_t>(row);
  const RowRef r = row_at(g);
  const std::size_t n = layouts_.size();
  if (r.beyond) return RolltuiTextPos{n - 1, layouts_[n - 1]->text().size(), 0};
  if (r.gap) {
    if (r.entry > 0) return RolltuiTextPos{r.entry - 1, layouts_[r.entry - 1]->text().size(), 0};
    return RolltuiTextPos{0, 0, 0};
  }
  return hit_row(r, x);
}

std::string RolltuiTranscript::selected_text() const {
  if (!sel_.active || layouts_.empty()) return {};
  const RolltuiTextPos f = sel_.first(), l = sel_.last();
  std::string out;
  for (std::size_t e = f.entry; e <= l.entry && e < layouts_.size(); ++e) {
    const std::string_view text = layouts_[e]->text();
    std::size_t b = 0, en = 0;
    if (!sel_.range_in(e, text.size(), b, en)) continue;
    if (e > f.entry) out += '\n';
    out.append(text, b, en - b);
  }
  return out;
}

bool RolltuiTranscript::copy_selection() {
  if (!sel_.active) return false;
  if (copy_fn_) {
    const std::string s = selected_text();
    copy_fn_(copy_ctx_, s.data(), s.size());
  }
  return true;
}

void RolltuiTranscript::begin_drag(int x, int y, bool shift, std::uint64_t now_ms, const RolltuiDocument& doc) {
  // A click on the "▼ N more" marker scrolls to the bottom and re-engages follow. Until
  // Phase 12 m5 it was painted and nothing more, so clicking it started a drag-SELECT —
  // a control-shaped thing doing something unrelated, which is the same defect one rung
  // down from a scrollbar you cannot grab. Checked before the fold summary because it
  // sits on the last row, over whatever is there.
  {
    const std::string marker = scroll_marker_text(lines_below(), text_area_.w, opt_.ambiguous_wide);
    if (!marker.empty() && area_.h > 0 && y == area_.y + area_.h - 1) {
      const int mw = unicode::display_width(marker, opt_.ambiguous_wide);
      const int right = text_area_.x + text_area_.w;
      if (x >= right - mw && x < right) {
        scroll_to_bottom();
        click_ = {};
        return;
      }
    }
  }
  std::optional<RolltuiTextPos> pos = hit(x, y);
  if (!pos) return;
  // A click on a summary line toggles the fold and selects nothing. A code block's own
  // header and "▼ N more" rows do the same one rung down (m5b) — over the WHOLE row,
  // because those rows carry nothing else and an x range is one more thing to get wrong
  // at a degenerate width.
  {
    const int row = std::clamp(y - area_.y, 0, std::max(area_.h - 1, 0));
    const RowRef r = row_at(top_line() + static_cast<std::size_t>(row));
    if (!r.gap && !r.beyond && r.entry < doc.size()) {
      if (r.line == 0 && doc[r.entry].foldable) {
        toggle_fold(doc[r.entry]);
        click_ = {};
        return;
      }
      for (const markdown::CodeBlockInfo& b : layouts_[r.entry]->code_blocks()) {
        if (b.header_line == r.line) {
          set_code_folded(view(doc[r.entry].id), b.index, !b.folded);
          click_ = {};
          return;
        }
        if (b.marker_line == r.line) {  // the cap's marker: show the rest of THIS block
          set_code_uncapped(view(doc[r.entry].id), b.index, true);
          click_ = {};
          return;
        }
      }
    }
  }
  if (shift) {
    if (!sel_.active) sel_.anchor = *pos;
    sel_.head = *pos;
    sel_.active = true;
  } else {
    const bool paired = click_.count > 0 && now_ms >= click_.at_ms && now_ms - click_.at_ms <= opt_.multi_click_ms &&
                        std::abs(x - click_.x) <= 1 && y == click_.y;
    click_.count = paired ? click_.count + 1 : 1;
    if (click_.count > 3) click_.count = 1;
    click_.at_ms = now_ms;
    click_.x = x;
    click_.y = y;
    if (click_.count >= 2) {
      const std::string_view text = layouts_[pos->entry]->text();
      const std::size_t off = std::min(pos->offset, text.size());
      std::size_t b, en;
      unit_around(text, off, click_.count == 2, b, en);
      drag_.origin_entry = pos->entry;
      drag_.origin_begin = b;
      drag_.origin_end = en;
      sel_ = {{pos->entry, b, 0}, {pos->entry, en, 0}, true};
    } else {
      sel_ = {*pos, *pos, true};
    }
  }
  drag_.active = true;
  drag_.outside = false;
  drag_.x = x;
  drag_.y = y;
}

// The word (UAX #29) or logical line containing byte `off` of `text`.
void RolltuiTranscript::unit_around(std::string_view text, std::size_t off, bool word, std::size_t& b, std::size_t& en) {
  if (word) {
    unicode::ByteRange w = unicode::word_range(text, off);
    b = w.begin;
    en = w.end;
    return;
  }
  std::size_t nl = off == 0 ? std::string_view::npos : text.rfind('\n', off - 1);
  b = (nl == std::string_view::npos) ? 0 : nl + 1;
  en = text.find('\n', off);
  if (en == std::string_view::npos) en = text.size();
}

void RolltuiTranscript::drag_to(int x, int y) {
  if (!drag_.active) return;
  drag_.x = x;
  drag_.y = y;
  int row = y - area_.y;
  drag_.outside = row < 0 || row >= area_.h;
  row = std::clamp(row, 0, std::max(area_.h - 1, 0));
  std::optional<RolltuiTextPos> pos = hit(x, area_.y + row);
  if (!pos) return;
  sel_.active = true;
  // After a double/triple click the selection grows by whole words/lines while the
  // pointer stays in the same entry; elsewhere it grows by graphemes from the unit.
  if (click_.count >= 2 && pos->entry == drag_.origin_entry) {
    const std::string_view text = layouts_[pos->entry]->text();
    std::size_t b, en;
    unit_around(text, std::min(pos->offset, text.size()), click_.count == 2, b, en);
    if (pos->offset < drag_.origin_begin) {
      sel_.anchor = {pos->entry, drag_.origin_end, 0};
      sel_.head = {pos->entry, b, 0};
    } else {
      sel_.anchor = {pos->entry, drag_.origin_begin, 0};
      sel_.head = {pos->entry, en, 0};
    }
    return;
  }
  if (click_.count >= 2) {
    const RolltuiTextPos origin_first{drag_.origin_entry, drag_.origin_begin, 0};
    sel_.anchor = rolltui_text_pos_less(&*pos, &origin_first) ? RolltuiTextPos{drag_.origin_entry, drag_.origin_end, 0}
                                                              : origin_first;
  }
  sel_.head = *pos;
}

void RolltuiTranscript::tick() {
  if (!wants_tick()) return;
  int dist = drag_.y < area_.y ? area_.y - drag_.y : drag_.y - (area_.y + area_.h - 1);
  dist = std::clamp(dist, 1, std::max(area_.h, 1));
  scroll_by(drag_.y < area_.y ? -dist : dist);
  const int row = drag_.y < area_.y ? 0 : std::max(area_.h - 1, 0);
  if (std::optional<RolltuiTextPos> pos = hit(drag_.x, area_.y + row)) sel_.head = *pos;
}

void RolltuiTranscript::end_drag() {
  if (!drag_.active) return;
  drag_.active = false;
  drag_.outside = false;
  if (sel_.active && sel_.anchor == sel_.head && click_.count <= 1) {
    sel_ = {};  // a plain click (or a Shift+click with nothing to extend) selects nothing
    return;
  }
  copy_selection();
}

bool RolltuiTranscript::handle(const RolltuiEvent& e, const RolltuiDocument& doc, std::uint64_t now_ms,
                               const RolltuiBindings* bindings, const RolltuiTranscriptActions& A) {
  if (e.kind == ROLLTUI_EVENT_MOUSE) {
    const RolltuiMouseEvent& m = e.mouse;
    using K = RolltuiMouseEvent::Kind;
    switch (m.kind) {
      case K::WheelUp: scroll_by(-static_cast<long>(opt_.wheel_lines)); return true;
      case K::WheelDown: scroll_by(static_cast<long>(opt_.wheel_lines)); return true;
      case K::WheelLeft:
      case K::WheelRight: return false;
      case K::Press:
        if (m.button != 1) return false;
        begin_drag(m.x, m.y, m.shift != 0, now_ms, doc);
        return true;
      case K::Drag:
        if (!drag_.active) return false;
        drag_to(m.x, m.y);
        return true;
      case K::Release: {
        if (!drag_.active) return false;
        // A release where the pointer already is changes nothing (so a double-click's word
        // is not narrowed to the cell under the button).
        if (m.x != drag_.x || m.y != drag_.y) drag_to(m.x, m.y);
        end_drag();
        return true;
      }
      case K::Move: return false;
    }
    return false;
  }
  if (e.kind == ROLLTUI_EVENT_KEY) {
    std::size_t alen = 0;
    const char* a = rolltui_bindings_action_for(bindings, &e.key, "transcript", 10, &alen);
    if (!a) return false;
    const std::string_view action(a, alen);
    if (action == A.page_up) { scroll_page(-1); return true; }
    if (action == A.page_down) { scroll_page(1); return true; }
    if (action == A.top) { scroll_to_top(); return true; }
    if (action == A.bottom) { scroll_to_bottom(); return true; }
    if (action == A.line_up) { scroll_by(-1); return true; }
    if (action == A.line_down) { scroll_by(1); return true; }
    if (action == A.find_next) return find_next();
    if (action == A.find_prev) return find_prev();
    if (action == A.fold) return toggle_fold_nearest_top(doc);
    if (action == A.copy) return copy_selection();
    if (action == A.clear_selection && sel_.active) {
      clear_selection();
      return true;
    }
  }
  return false;
}

// ---- the boundary -----------------------------------------------------------------------------

extern "C" RolltuiTranscript* rolltui_transcript_new(void) {
  return std::make_unique<RolltuiTranscript>().release();
}

extern "C" void rolltui_transcript_free(RolltuiTranscript* t) {
  const std::unique_ptr<RolltuiTranscript> owned(t);
}

extern "C" void rolltui_transcript_set_copy(RolltuiTranscript* t, RolltuiCopyFn fn, void* ctx) {
  t->copy_fn_ = fn;
  t->copy_ctx_ = ctx;
}

extern "C" void rolltui_transcript_layout(RolltuiTranscript* t, const RolltuiDocument* doc, RolltuiRect area,
                                          const RolltuiTranscriptOptions* opt) {
  t->layout(*doc, area, *opt);
}

extern "C" void rolltui_transcript_set_roles(RolltuiTranscript* t, const RolltuiTranscriptRoles* roles) {
  t->roles_ = *roles;
}

extern "C" void rolltui_transcript_draw(const RolltuiTranscript* t, RolltuiFrame* f,
                                        RolltuiDrawScratch* d, const RolltuiStyle* styles) {
  t->draw(f, d, styles);
}

extern "C" int rolltui_transcript_handle(RolltuiTranscript* t, const RolltuiEvent* e,
                                         const RolltuiDocument* doc, unsigned long long now_ms,
                                         const RolltuiBindings* bindings,
                                         const RolltuiTranscriptActions* actions) {
  return t->handle(*e, *doc, now_ms, bindings, *actions) ? 1 : 0;
}

extern "C" int rolltui_transcript_wants_tick(const RolltuiTranscript* t) { return t->wants_tick() ? 1 : 0; }
extern "C" void rolltui_transcript_tick(RolltuiTranscript* t) { t->tick(); }

extern "C" void rolltui_transcript_scroll_by(RolltuiTranscript* t, long lines) { t->scroll_by(lines); }
extern "C" void rolltui_transcript_scroll_page(RolltuiTranscript* t, int d) { t->scroll_page(d); }
extern "C" void rolltui_transcript_scroll_to_top(RolltuiTranscript* t) { t->scroll_to_top(); }
extern "C" void rolltui_transcript_scroll_to_bottom(RolltuiTranscript* t) { t->scroll_to_bottom(); }
extern "C" void rolltui_transcript_scroll(const RolltuiTranscript* t, RolltuiScrollAnchor* out) {
  *out = t->scroll_;
}
extern "C" std::size_t rolltui_transcript_total_lines(const RolltuiTranscript* t) { return t->total_; }
extern "C" std::size_t rolltui_transcript_top_line(const RolltuiTranscript* t) { return t->top_line(); }
extern "C" std::size_t rolltui_transcript_lines_below(const RolltuiTranscript* t) { return t->lines_below(); }
extern "C" int rolltui_transcript_viewport_height(const RolltuiTranscript* t) { return t->area_.h; }

extern "C" int rolltui_transcript_is_folded(const RolltuiTranscript* t, const RolltuiDocEntry* e) {
  return t->is_folded(*e) ? 1 : 0;
}
extern "C" void rolltui_transcript_set_folded(RolltuiTranscript* t, const char* id, std::size_t len,
                                              int folded) {
  t->set_folded(std::string_view(id, len), folded != 0);
}
extern "C" int rolltui_transcript_toggle_fold_nearest_top(RolltuiTranscript* t, const RolltuiDocument* doc) {
  return t->toggle_fold_nearest_top(*doc) ? 1 : 0;
}
extern "C" void rolltui_transcript_set_code_folded(RolltuiTranscript* t, const char* id, std::size_t len,
                                                   std::size_t block, int folded) {
  t->set_code_folded(std::string_view(id, len), block, folded != 0);
}
extern "C" void rolltui_transcript_set_code_uncapped(RolltuiTranscript* t, const char* id, std::size_t len,
                                                     std::size_t block, int uncapped) {
  t->set_code_uncapped(std::string_view(id, len), block, uncapped != 0);
}

extern "C" int rolltui_transcript_set_query(RolltuiTranscript* t, const char* q, std::size_t len) {
  return t->set_query(std::string_view(q ? q : "", len)) ? 1 : 0;
}
extern "C" const char* rolltui_transcript_query(const RolltuiTranscript* t, std::size_t* len) {
  if (len) *len = t->query_.size();
  return t->query_.c_str();
}
extern "C" std::size_t rolltui_transcript_match_count(const RolltuiTranscript* t) {
  return t->matches_.size();
}
extern "C" int rolltui_transcript_match_at(const RolltuiTranscript* t, std::size_t i, RolltuiFindMatch* out) {
  if (i >= t->matches_.size()) return 0;
  *out = t->matches_[i];
  return 1;
}
extern "C" std::size_t rolltui_transcript_current_match_number(const RolltuiTranscript* t) {
  return t->current_ ? *t->current_ + 1 : 0;
}
extern "C" int rolltui_transcript_current_match(const RolltuiTranscript* t, RolltuiFindMatch* out) {
  const RolltuiFindMatch* m = t->current_match();
  if (!m) return 0;
  *out = *m;
  return 1;
}
extern "C" int rolltui_transcript_find_next(RolltuiTranscript* t) { return t->find_next() ? 1 : 0; }
extern "C" int rolltui_transcript_find_prev(RolltuiTranscript* t) { return t->find_prev() ? 1 : 0; }

extern "C" void rolltui_transcript_selection(const RolltuiTranscript* t, RolltuiSelection* out) {
  *out = t->sel_;
}
extern "C" void rolltui_transcript_clear_selection(RolltuiTranscript* t) { t->clear_selection(); }
extern "C" void rolltui_transcript_select(RolltuiTranscript* t, RolltuiTextPos anchor, RolltuiTextPos head) {
  t->sel_ = {anchor, head, 1};
}
extern "C" int rolltui_transcript_hit(const RolltuiTranscript* t, int x, int y, RolltuiTextPos* out) {
  const std::optional<RolltuiTextPos> p = t->hit(x, y);
  if (!p) return 0;
  *out = *p;
  return 1;
}
extern "C" void rolltui_transcript_selected_text(const RolltuiTranscript* t, RolltuiStr* out) {
  *out = t->selected_text();
}
extern "C" int rolltui_transcript_copy_selection(RolltuiTranscript* t) {
  return t->copy_selection() ? 1 : 0;
}

extern "C" void rolltui_transcript_stats(const RolltuiTranscript* t, RolltuiTranscriptStats* out) {
  *out = t->stats_;
}
extern "C" const RolltuiEntryLayout* rolltui_transcript_layout_of(const RolltuiTranscript* t,
                                                                 std::size_t entry) {
  return t->layout_of(entry);
}
extern "C" void rolltui_transcript_area(const RolltuiTranscript* t, RolltuiRect* out) { *out = t->area_; }
extern "C" void rolltui_transcript_text_area(const RolltuiTranscript* t, RolltuiRect* out) {
  *out = t->text_area_;
}
