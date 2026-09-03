// rolltui/Transcript.cpp — see Transcript.hpp for the rules.
#include "rolltui/Transcript.hpp"

#include <algorithm>
#include <chrono>

#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"

namespace rolltui {

using markdown::kNoSource;
using markdown::Span;
using markdown::StyledLine;

// ---- selection model -------------------------------------------------------------

// Entry, then offset, then length — so of two positions at the same offset the one
// that covers a grapheme is `last()`, and range_in includes it.
bool operator<(const TextPos& a, const TextPos& b) {
  if (a.entry != b.entry) return a.entry < b.entry;
  if (a.offset != b.offset) return a.offset < b.offset;
  return a.length < b.length;
}

bool Selection::range_in(std::size_t entry, std::size_t len, std::size_t& begin, std::size_t& end) const {
  if (!active) return false;
  const TextPos f = first(), l = last();
  if (entry < f.entry || entry > l.entry) return false;
  begin = (entry == f.entry) ? std::min(f.offset, len) : 0;
  end = (entry == l.entry) ? std::min(l.offset + l.length, len) : len;
  if (end < begin) end = begin;
  return true;
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
  for (const Span& sp : line.spans) {
    std::size_t k = 0;
    for (const unicode::Grapheme& g : unicode::graphemes(sp.text, ambiguous)) {
      if (g.width > 0) f(sp, k, std::string_view(sp.text).substr(g.offset, g.length), g.width);
      ++k;
    }
  }
}

std::uint32_t source_of(const Span& sp, std::size_t k) {
  if (sp.sources.empty()) return kNoSource;
  return sp.sources[std::min(k, sp.sources.size() - 1)];
}

Span chrome(std::string text, Role role, bool ambiguous) {
  Span s;
  s.role = role;
  std::size_t clusters = 0;
  for (const unicode::Grapheme& g : unicode::graphemes(text, ambiguous)) { s.width += g.width; ++clusters; }
  s.sources.assign(clusters, kNoSource);
  s.text = std::move(text);
  return s;
}

void append(StyledLine& line, Span s) {
  if (s.text.empty()) return;
  line.width += s.width;
  line.spans.push_back(std::move(s));
}

std::string spaces(int n) { return std::string(static_cast<std::size_t>(std::max(n, 0)), ' '); }

}  // namespace

// ---- layout ----------------------------------------------------------------------

EntryLayout Transcript::lay_out(const DocEntry& e, int width, const TranscriptOptions& opt, bool folded) {
  EntryLayout L;
  L.folded = folded && e.foldable;
  const bool amb = opt.ambiguous_wide;
  const int prefix_w = unicode::display_width(e.prefix, amb);
  const int inner = std::max(width - prefix_w, 1);

  std::vector<StyledLine> body;
  if (e.markdown) {
    markdown::RenderOptions ro;
    ro.width = inner;
    ro.ambiguous_wide = amb;
    ro.tab_width = opt.tab_width;
    ro.base = e.role;
    markdown::Rendered r = markdown::render_text(e.text, ro);
    body = std::move(r.lines);
    L.text = std::move(r.text);
  } else {
    WrapOptions wo;
    wo.ambiguous_wide = amb;
    wo.tab_width = opt.tab_width;
    for (const Line& l : wrap(e.text, inner, wo)) {
      StyledLine sl;
      if (l.indent > 0) append(sl, chrome(spaces(l.indent), e.role, amb));
      Span s;
      s.text = l.text;
      s.width = l.width;
      s.role = e.role;
      for (const WrapGrapheme& g : l.graphemes) s.sources.push_back(static_cast<std::uint32_t>(g.source_offset));
      append(sl, std::move(s));
      body.push_back(std::move(sl));
    }
    L.text = e.text;
  }
  if (body.empty()) body.push_back({});

  auto with_prefix = [&](StyledLine& sl, bool first) {
    if (prefix_w <= 0) return;
    if (first) append(sl, chrome(e.prefix, e.prefix_role, amb));
    else append(sl, chrome(spaces(prefix_w), e.role, amb));
  };
  auto add_body = [&](const StyledLine& b, bool first) {
    StyledLine sl;
    with_prefix(sl, first);
    for (const Span& s : b.spans) append(sl, s);
    L.lines.push_back(std::move(sl));
  };

  if (e.foldable) {
    StyledLine s;
    with_prefix(s, true);
    append(s, chrome(L.folded ? "\xE2\x96\xB8 " : "\xE2\x96\xBE ", Role::text_muted, amb));  // ▸ ▾
    append(s, chrome(e.summary, e.role, amb));
    if (L.folded)
      append(s, chrome(" (" + std::to_string(body.size()) + (body.size() == 1 ? " line)" : " lines)"), Role::text_muted, amb));
    L.lines.push_back(std::move(s));
    if (L.folded) {
      L.hidden_lines = body.size();
      L.text = e.summary;
      return L;
    }
    for (const StyledLine& b : body) add_body(b, false);
  } else {
    for (std::size_t k = 0; k < body.size(); ++k) add_body(body[k], k == 0);
  }
  return L;
}

std::size_t Transcript::block_len(std::size_t entry) const {
  return (entry > 0 ? static_cast<std::size_t>(std::max(opt_.gap, 0)) : 0) + layouts_[entry]->lines.size();
}

std::size_t Transcript::max_top() const {
  const std::size_t h = static_cast<std::size_t>(std::max(area_.h, 0));
  return total_ > h ? total_ - h : 0;
}

void Transcript::set_top(std::size_t top) {
  if (starts_.empty()) { scroll_ = {0, 0, true}; return; }
  top = std::min(top, max_top());
  auto it = std::upper_bound(starts_.begin(), starts_.end(), top);
  std::size_t e = static_cast<std::size_t>(it - starts_.begin()) - 1;
  scroll_.entry = e;
  scroll_.line = top - starts_[e];
  scroll_.follow = top >= max_top();
}

std::size_t Transcript::top_line() const {
  if (starts_.empty()) return 0;
  std::size_t e = std::min(scroll_.entry, starts_.size() - 1);
  return starts_[e] + scroll_.line;
}

std::size_t Transcript::lines_below() const {
  const std::size_t shown_end = top_line() + static_cast<std::size_t>(std::max(area_.h, 0));
  return total_ > shown_end ? total_ - shown_end : 0;
}

void Transcript::build(const Document& doc, int width) {
  const std::size_t n = doc.entries.size();
  layouts_.assign(n, nullptr);
  starts_.assign(n, 0);
  for (auto& [id, c] : cache_) c.seen = false;
  std::size_t g = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const DocEntry& e = doc.entries[i];
    const bool folded = e.foldable && is_folded(e);
    const CacheKey key{e.version, width, opt_.ambiguous_wide, opt_.tab_width, folded};
    auto it = cache_.find(e.id);
    if (it == cache_.end() || !(it->second.key == key)) {
      Cached c;
      c.key = key;
      c.layout = lay_out(e, width, opt_, folded);
      it = cache_.insert_or_assign(e.id, std::move(c)).first;
      ++stats_.entries_relaid;
    }
    it->second.seen = true;
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

void Transcript::layout(const Document& doc, Rect area, const TranscriptOptions& opt) {
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

bool matches_at(const std::string& hay, std::size_t at, const std::string& needle) {
  if (at + needle.size() > hay.size()) return false;
  for (std::size_t k = 0; k < needle.size(); ++k)
    if (lower_ascii(hay[at + k]) != lower_ascii(needle[k])) return false;
  return true;
}

}  // namespace

bool Transcript::set_query(std::string_view q) {
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

const std::string& Transcript::searchable_text(const DocEntry& e, std::size_t entry, int width) {
  // For everything but a FOLDED entry the drawn layout's text already is the unfolded
  // logical text, so the common case costs nothing.
  if (!(e.foldable && is_folded(e))) return layouts_[entry]->text;
  const CacheKey key{e.version, width, opt_.ambiguous_wide, opt_.tab_width, /*folded=*/false};
  auto it = find_text_.find(e.id);
  if (it == find_text_.end() || !(it->second.key == key)) {
    FindText ft;
    ft.key = key;
    ft.text = lay_out(e, width, opt_, /*folded=*/false).text;
    it = find_text_.insert_or_assign(e.id, std::move(ft)).first;
  }
  return it->second.text;
}

void Transcript::recompute_matches(const Document& doc, int width) {
  // Where the user WAS, so a recompute forced by a streaming answer does not silently
  // move them back to the first match while they are stepping through.
  const std::optional<FindMatch> was = current_match() ? std::optional<FindMatch>(*current_match()) : std::nullopt;
  matches_.clear();
  current_.reset();
  if (query_.empty()) return;
  for (std::size_t i = 0; i < doc.entries.size(); ++i) {
    const std::string& hay = searchable_text(doc.entries[i], i, width);
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

std::size_t Transcript::line_of_offset(std::size_t entry, std::size_t offset) const {
  const EntryLayout* L = layout_of(entry);
  if (!L || L->lines.empty()) return 0;
  std::size_t best = 0;
  for (std::size_t i = 0; i < L->lines.size(); ++i) {
    bool any = false;
    std::size_t lo = 0, hi = 0;
    for_each_cell(L->lines[i], opt_.ambiguous_wide, [&](const Span& sp, std::size_t k, std::string_view gt, int) {
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

void Transcript::reveal_current(const Document& doc, int width) {
  const FindMatch* m = current_match();
  if (!m || m->entry >= doc.entries.size()) return;
  // A match inside a folded block: unfold it, then re-run the build, because every line
  // number below the entry has just moved.
  const DocEntry& e = doc.entries[m->entry];
  if (e.foldable && is_folded(e)) {
    set_folded(e.id, false);
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

bool Transcript::find_next() {
  if (matches_.empty()) return false;
  current_ = current_ ? (*current_ + 1) % matches_.size() : 0;
  reveal_ = true;
  return true;
}

bool Transcript::find_prev() {
  if (matches_.empty()) return false;
  current_ = current_ && *current_ > 0 ? *current_ - 1 : matches_.size() - 1;
  reveal_ = true;
  return true;
}

Transcript::RowRef Transcript::row_at(std::size_t global) const {
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

const EntryLayout* Transcript::layout_of(std::size_t entry) const {
  return entry < layouts_.size() ? layouts_[entry] : nullptr;
}

// ---- drawing ---------------------------------------------------------------------

void Transcript::draw(Frame& frame, const Theme& theme) const {
  const Rect area = area_.intersect(frame.bounds());
  if (area.empty()) return;
  frame.fill(area, theme.style(Role::background));
  const bool amb = opt_.ambiguous_wide;
  const Style& sel_style = theme.style(Role::selection);
  auto styled = [&](Role role, bool selected) {
    const Style s = theme.style(role);
    return selected ? overlay_style(s, sel_style) : s;
  };
  // A find highlight is the SAME range test the selection uses, on the same per-cell
  // source offsets — which is why a match that wraps lights up on both rows without
  // anything here knowing what a row is.
  const Style& match_style = theme.style(Role::find_match);
  const Style& current_style = theme.style(Role::find_current);
  const FindMatch* cur = current_match();
  const std::size_t top = top_line();
  const int right = text_area_.x + text_area_.w;
  for (int row = 0; row < area_.h; ++row) {
    const std::size_t g = top + static_cast<std::size_t>(row);
    if (g >= total_) break;
    const RowRef r = row_at(g);
    if (r.gap || r.beyond) continue;
    const int y = area_.y + row;
    if (y < 0 || y >= frame.height()) continue;
    const StyledLine& line = layouts_[r.entry]->lines[r.line];
    // Selection state of this line: the byte range within the entry, and whether the
    // line's text lies wholly inside it (then its chrome highlights too).
    std::size_t sb = 0, se = 0;
    const std::size_t len = layouts_[r.entry]->text.size();
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
    const FindMatch* mb = matches_.data();
    const FindMatch* me = mb;
    if (!matches_.empty()) {
      auto by_entry = [](const FindMatch& m, std::size_t e) { return m.entry < e; };
      auto entry_by = [](std::size_t e, const FindMatch& m) { return e < m.entry; };
      mb = std::lower_bound(matches_.begin(), matches_.end(), r.entry, by_entry).base();
      me = std::upper_bound(matches_.begin(), matches_.end(), r.entry, entry_by).base();
    }
    int x = text_area_.x;
    bool stop = false;
    for_each_cell(line, amb, [&](const Span& sp, std::size_t k, std::string_view gt, int w) {
      if (stop || x + w > right) { stop = true; return; }
      const std::uint32_t src = source_of(sp, k);
      const bool selected = fully || (in_sel && src != kNoSource && src >= sb && src < se);
      Style st = styled(sp.role, selected);
      // The selection WINS where they overlap (Transcript.hpp's FIND): it is the user's
      // most recent direct act. Otherwise the current match beats the other matches.
      if (!selected && src != kNoSource) {
        for (const FindMatch* m = mb; m != me; ++m) {
          if (src >= m->offset && src < m->offset + m->length) {
            st = overlay_style(st, (cur && *m == *cur) ? current_style : match_style);
            break;
          }
        }
      }
      const std::uint32_t link = sp.href.empty() ? 0 : frame.link_id(sp.href);
      x += frame.put(x, y, gt, w, st, link);
    });
  }
  const std::size_t below = lines_below();
  if (below > 0 && text_area_.w >= 8 && area_.h > 0) {
    std::string marker = "\xE2\x96\xBC " + std::to_string(below) + " more ";  // ▼
    int mw = unicode::display_width(marker, amb);
    frame.put_text(right - mw, area_.y + area_.h - 1, marker, theme.style(Role::scroll_marker), mw, amb);
  }
}

// ---- scrolling -------------------------------------------------------------------

void Transcript::scroll_by(long lines) {
  long t = static_cast<long>(top_line()) + lines;
  if (t < 0) t = 0;
  set_top(static_cast<std::size_t>(t));
}

void Transcript::scroll_page(int direction) {
  const long page = std::max(area_.h - 1, 1);
  scroll_by(direction < 0 ? -page : page);
}

void Transcript::scroll_to_top() { set_top(0); }

void Transcript::scroll_to_bottom() {
  set_top(max_top());
  scroll_.follow = true;
}

// ---- folding ---------------------------------------------------------------------

bool Transcript::is_folded(const DocEntry& e) const {
  auto it = fold_override_.find(e.id);
  return it != fold_override_.end() ? it->second : e.folded;
}

bool Transcript::toggle_fold_nearest_top(const Document& doc) {
  const std::size_t top = top_line();
  for (int row = 0; row < area_.h; ++row) {
    const std::size_t g = top + static_cast<std::size_t>(row);
    if (g >= total_) break;
    const RowRef r = row_at(g);
    if (r.gap || r.beyond || r.line != 0 || r.entry >= doc.entries.size()) continue;
    if (!doc.entries[r.entry].foldable) continue;
    toggle_fold(doc.entries[r.entry]);
    return true;
  }
  return false;
}

// ---- selection -------------------------------------------------------------------

std::optional<TextPos> Transcript::hit_row(const RowRef& r, int x) const {
  const StyledLine& line = layouts_[r.entry]->lines[r.line];
  const bool amb = opt_.ambiguous_wide;
  std::optional<TextPos> at, before, after, last;
  int cx = text_area_.x;
  for_each_cell(line, amb, [&](const Span& sp, std::size_t k, std::string_view gt, int w) {
    const std::uint32_t src = source_of(sp, k);
    const bool contains = x >= cx && x < cx + w;
    if (src != kNoSource) {
      TextPos p{r.entry, src, gt.size()};
      last = p;
      if (contains) at = p;
      else if (cx + w <= x) before = TextPos{r.entry, src + gt.size(), 0};
      else if (!after) after = TextPos{r.entry, src, 0};
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
    std::optional<TextPos> found;
    for_each_cell(layouts_[r.entry]->lines[li], amb, [&](const Span& sp, std::size_t k, std::string_view gt, int) {
      const std::uint32_t src = source_of(sp, k);
      if (src != kNoSource) found = TextPos{r.entry, src + gt.size(), 0};
    });
    if (found) return found;
  }
  return TextPos{r.entry, 0, 0};
}

std::optional<TextPos> Transcript::hit(int x, int y) const {
  if (layouts_.empty() || y < area_.y) return std::nullopt;
  int row = y - area_.y;
  if (row >= area_.h) row = std::max(area_.h - 1, 0);
  const std::size_t g = top_line() + static_cast<std::size_t>(row);
  const RowRef r = row_at(g);
  const std::size_t n = layouts_.size();
  if (r.beyond) return TextPos{n - 1, layouts_[n - 1]->text.size(), 0};
  if (r.gap) {
    if (r.entry > 0) return TextPos{r.entry - 1, layouts_[r.entry - 1]->text.size(), 0};
    return TextPos{0, 0, 0};
  }
  return hit_row(r, x);
}

std::string Transcript::selected_text() const {
  if (!sel_.active || layouts_.empty()) return {};
  const TextPos f = sel_.first(), l = sel_.last();
  std::string out;
  for (std::size_t e = f.entry; e <= l.entry && e < layouts_.size(); ++e) {
    const std::string& text = layouts_[e]->text;
    std::size_t b = 0, en = 0;
    if (!sel_.range_in(e, text.size(), b, en)) continue;
    if (e > f.entry) out += '\n';
    out.append(text, b, en - b);
  }
  return out;
}

bool Transcript::copy_selection() {
  if (!sel_.active) return false;
  if (on_copy) on_copy(selected_text());
  return true;
}

void Transcript::begin_drag(int x, int y, bool shift, std::uint64_t now_ms, const Document& doc) {
  std::optional<TextPos> pos = hit(x, y);
  if (!pos) return;
  // A click on a summary line toggles the fold and selects nothing.
  {
    const int row = std::clamp(y - area_.y, 0, std::max(area_.h - 1, 0));
    const RowRef r = row_at(top_line() + static_cast<std::size_t>(row));
    if (!r.gap && !r.beyond && r.line == 0 && r.entry < doc.entries.size() && doc.entries[r.entry].foldable) {
      toggle_fold(doc.entries[r.entry]);
      click_ = {};
      return;
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
      const std::string& text = layouts_[pos->entry]->text;
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
void Transcript::unit_around(const std::string& text, std::size_t off, bool word, std::size_t& b, std::size_t& en) {
  if (word) {
    unicode::ByteRange w = unicode::word_range(text, off);
    b = w.begin;
    en = w.end;
    return;
  }
  std::size_t nl = off == 0 ? std::string::npos : text.rfind('\n', off - 1);
  b = (nl == std::string::npos) ? 0 : nl + 1;
  en = text.find('\n', off);
  if (en == std::string::npos) en = text.size();
}

void Transcript::drag_to(int x, int y) {
  if (!drag_.active) return;
  drag_.x = x;
  drag_.y = y;
  int row = y - area_.y;
  drag_.outside = row < 0 || row >= area_.h;
  row = std::clamp(row, 0, std::max(area_.h - 1, 0));
  std::optional<TextPos> pos = hit(x, area_.y + row);
  if (!pos) return;
  sel_.active = true;
  // After a double/triple click the selection grows by whole words/lines while the
  // pointer stays in the same entry; elsewhere it grows by graphemes from the unit.
  if (click_.count >= 2 && pos->entry == drag_.origin_entry) {
    const std::string& text = layouts_[pos->entry]->text;
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
    const TextPos origin_first{drag_.origin_entry, drag_.origin_begin, 0};
    sel_.anchor = (*pos < origin_first) ? TextPos{drag_.origin_entry, drag_.origin_end, 0} : origin_first;
  }
  sel_.head = *pos;
}

void Transcript::tick() {
  if (!wants_tick()) return;
  int dist = drag_.y < area_.y ? area_.y - drag_.y : drag_.y - (area_.y + area_.h - 1);
  dist = std::clamp(dist, 1, std::max(area_.h, 1));
  scroll_by(drag_.y < area_.y ? -dist : dist);
  const int row = drag_.y < area_.y ? 0 : std::max(area_.h - 1, 0);
  if (std::optional<TextPos> pos = hit(drag_.x, area_.y + row)) sel_.head = *pos;
}

void Transcript::end_drag() {
  if (!drag_.active) return;
  drag_.active = false;
  drag_.outside = false;
  if (sel_.active && sel_.anchor == sel_.head && click_.count <= 1) {
    sel_ = {};  // a plain click (or a Shift+click with nothing to extend) selects nothing
    return;
  }
  copy_selection();
}

bool Transcript::handle(const Event& e, const Document& doc, std::uint64_t now_ms, const Bindings& bindings) {
  if (const MouseEvent* m = std::get_if<MouseEvent>(&e)) {
    using K = MouseEvent::Kind;
    switch (m->kind) {
      case K::WheelUp: scroll_by(-static_cast<long>(opt_.wheel_lines)); return true;
      case K::WheelDown: scroll_by(static_cast<long>(opt_.wheel_lines)); return true;
      case K::WheelLeft: case K::WheelRight: return false;
      case K::Press:
        if (m->button != 1) return false;
        begin_drag(m->x, m->y, m->shift, now_ms, doc);
        return true;
      case K::Drag:
        if (!drag_.active) return false;
        drag_to(m->x, m->y);
        return true;
      case K::Release:
        if (!drag_.active) return false;
        // A release where the pointer already is changes nothing (so a double-click's
        // word is not narrowed to the cell under the button).
        if (m->x != drag_.x || m->y != drag_.y) drag_to(m->x, m->y);
        end_drag();
        return true;
      case K::Move: return false;
    }
    return false;
  }
  if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) {
    const std::string_view action = bindings.action_for(*k, "transcript");
    if (action == "transcript.page_up") { scroll_page(-1); return true; }
    if (action == "transcript.page_down") { scroll_page(1); return true; }
    if (action == "transcript.top") { scroll_to_top(); return true; }
    if (action == "transcript.bottom") { scroll_to_bottom(); return true; }
    if (action == "transcript.line_up") { scroll_by(-1); return true; }
    if (action == "transcript.line_down") { scroll_by(1); return true; }
    if (action == "transcript.find_next") return find_next();
    if (action == "transcript.find_prev") return find_prev();
    if (action == "transcript.fold") return toggle_fold_nearest_top(doc);
    if (action == "transcript.copy") return copy_selection();
    if (action == "transcript.clear_selection" && sel_.active) { clear_selection(); return true; }
  }
  return false;
}

}  // namespace rolltui
