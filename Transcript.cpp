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
  const std::size_t n = doc.entries.size();
  layouts_.assign(n, nullptr);
  starts_.assign(n, 0);
  stats_.entries_relaid = 0;
  for (auto& [id, c] : cache_) c.seen = false;
  std::size_t g = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const DocEntry& e = doc.entries[i];
    const bool folded = e.foldable && is_folded(e);
    const CacheKey key{e.version, width, opt.ambiguous_wide, opt.tab_width, folded};
    auto it = cache_.find(e.id);
    if (it == cache_.end() || !(it->second.key == key)) {
      Cached c;
      c.key = key;
      c.layout = lay_out(e, width, opt, folded);
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
  stats_.total_lines = total_;
  stats_.cache_size = cache_.size();
  stats_.layout_us = static_cast<long>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
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
    Style s = theme.style(role);
    if (!selected) return s;
    if (sel_style.fg.kind != Color::Kind::None) s.fg = sel_style.fg;
    if (sel_style.bg.kind != Color::Kind::None) s.bg = sel_style.bg;
    s.bold |= sel_style.bold;
    s.italic |= sel_style.italic;
    s.underline |= sel_style.underline;
    s.dim |= sel_style.dim;
    s.reverse |= sel_style.reverse;
    return s;
  };
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
    int x = text_area_.x;
    bool stop = false;
    for_each_cell(line, amb, [&](const Span& sp, std::size_t k, std::string_view gt, int w) {
      if (stop || x + w > right) { stop = true; return; }
      const std::uint32_t src = source_of(sp, k);
      const bool selected = fully || (in_sel && src != kNoSource && src >= sb && src < se);
      const std::uint32_t link = sp.href.empty() ? 0 : frame.link_id(sp.href);
      x += frame.put(x, y, gt, w, styled(sp.role, selected), link);
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
    if (action == "transcript.fold") return toggle_fold_nearest_top(doc);
    if (action == "transcript.copy") return copy_selection();
    if (action == "transcript.clear_selection" && sel_.active) { clear_selection(); return true; }
  }
  return false;
}

}  // namespace rolltui
