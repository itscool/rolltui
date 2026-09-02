// rolltui/Input.cpp — see Input.hpp.
#include "rolltui/Input.hpp"

#include <algorithm>
#include <cstdlib>

namespace rolltui {

namespace {

// What may enter the text: CR LF and CR become LF; '\n' and '\t' pass; every other
// control character (and DEL) is dropped.
std::string sanitise(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c == '\r') {
      out.push_back('\n');
      if (i + 1 < in.size() && in[i + 1] == '\n') ++i;
      continue;
    }
    if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7F)) out.push_back(static_cast<char>(c));
  }
  return out;
}

bool is_space_at(const std::string& s, std::size_t pos) {
  return pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n');
}

}  // namespace

// ---- content -------------------------------------------------------------------------

void Input::retext(std::string t, std::size_t caret) {
  text_ = std::move(t);
  g_ = unicode::graphemes(text_, opt_.ambiguous_wide);
  caret_ = snap(caret);
  if (sel_.active) {
    sel_.anchor = snap(sel_.anchor);
    sel_.head = snap(sel_.head);
  }
  dirty_ = true;
}

std::size_t Input::snap(std::size_t pos) const {
  pos = std::min(pos, text_.size());
  auto it = std::lower_bound(g_.begin(), g_.end(), pos,
                             [](const unicode::Grapheme& g, std::size_t p) { return g.offset < p; });
  return it == g_.end() ? text_.size() : it->offset;
}

std::size_t Input::prev_boundary(std::size_t pos) const {
  auto it = std::lower_bound(g_.begin(), g_.end(), pos,
                             [](const unicode::Grapheme& g, std::size_t p) { return g.offset < p; });
  if (it == g_.begin()) return 0;
  return (it - 1)->offset;
}

std::size_t Input::next_boundary(std::size_t pos) const {
  auto it = std::upper_bound(g_.begin(), g_.end(), pos,
                             [](std::size_t p, const unicode::Grapheme& g) { return p < g.offset; });
  return it == g_.end() ? text_.size() : it->offset;
}

std::size_t Input::line_start(std::size_t pos) const {
  if (pos == 0) return 0;
  const std::size_t nl = text_.rfind('\n', pos - 1);
  return nl == std::string::npos ? 0 : nl + 1;
}

std::size_t Input::line_end(std::size_t pos) const {
  const std::size_t nl = text_.find('\n', pos);
  return nl == std::string::npos ? text_.size() : nl;
}

void Input::set_text(std::string t) {
  sel_ = {};
  goal_col_.reset();
  std::string s = sanitise(t);
  const std::size_t n = s.size();
  retext(std::move(s), n);
}

void Input::clear() {
  set_text({});
  hist_pos_ = hist_.size();
  draft_.clear();
}

void Input::place(std::size_t pos, bool extend) {
  pos = snap(pos);
  if (extend) {
    if (!sel_.active) sel_.anchor = caret_;
    sel_.head = pos;
    sel_.active = true;
  } else {
    sel_ = {};
  }
  caret_ = pos;
}

void Input::set_caret(std::size_t byte, bool extend) {
  place(byte, extend);
  goal_col_.reset();
}

std::string Input::selected_text() const {
  if (sel_.empty()) return {};
  return text_.substr(sel_.begin(), sel_.end() - sel_.begin());
}

void Input::select_all() {
  if (text_.empty()) return;
  sel_ = {0, text_.size(), true};
  caret_ = text_.size();
  goal_col_.reset();
}

// ---- editing -------------------------------------------------------------------------

void Input::erase_range(std::size_t b, std::size_t e) {
  b = std::min(b, text_.size());
  e = std::min(e, text_.size());
  if (b >= e) return;
  std::string t = text_;
  t.erase(b, e - b);
  std::size_t caret = caret_;
  if (caret >= e) caret -= (e - b);
  else if (caret > b) caret = b;
  sel_ = {};
  retext(std::move(t), caret);
  goal_col_.reset();
}

void Input::insert(std::string_view utf8) {
  const std::string s = sanitise(utf8);
  erase_selection();
  if (s.empty()) return;
  std::string t = text_;
  t.insert(caret_, s);
  sel_ = {};
  retext(std::move(t), caret_ + s.size());
  goal_col_.reset();
}

bool Input::erase_selection() {
  if (sel_.empty()) {
    sel_ = {};
    return false;
  }
  erase_range(sel_.begin(), sel_.end());
  return true;
}

void Input::erase_backward() {
  if (erase_selection()) return;
  if (caret_ == 0) return;
  erase_range(prev_boundary(caret_), caret_);
}

void Input::erase_forward() {
  if (erase_selection()) return;
  if (caret_ >= text_.size()) return;
  erase_range(caret_, next_boundary(caret_));
}

std::size_t Input::word_left_of(std::size_t pos) const {
  pos = std::min(pos, text_.size());
  std::size_t p = pos;
  while (p > 0 && is_space_at(text_, prev_boundary(p))) p = prev_boundary(p);
  if (p == 0) return 0;
  return unicode::word_range(text_, prev_boundary(p)).begin;
}

std::size_t Input::word_right_of(std::size_t pos) const {
  pos = std::min(pos, text_.size());
  std::size_t p = pos;
  while (p < text_.size() && is_space_at(text_, p)) p = next_boundary(p);
  if (p >= text_.size()) return text_.size();
  return unicode::word_range(text_, p).end;
}

void Input::kill_word_backward() {
  if (erase_selection()) return;
  erase_range(word_left_of(caret_), caret_);
}

void Input::kill_word_forward() {
  if (erase_selection()) return;
  erase_range(caret_, word_right_of(caret_));
}

void Input::kill_to_line_start() {
  sel_ = {};
  erase_range(line_start(caret_), caret_);
}

void Input::kill_to_line_end() {
  sel_ = {};
  erase_range(caret_, line_end(caret_));
}

void Input::move_left(bool extend) {
  if (!extend && !sel_.empty()) { set_caret(sel_.begin(), false); return; }
  set_caret(caret_ == 0 ? 0 : prev_boundary(caret_), extend);
}

void Input::move_right(bool extend) {
  if (!extend && !sel_.empty()) { set_caret(sel_.end(), false); return; }
  set_caret(next_boundary(caret_), extend);
}

void Input::move_word_left(bool extend) { set_caret(word_left_of(caret_), extend); }
void Input::move_word_right(bool extend) { set_caret(word_right_of(caret_), extend); }
void Input::move_line_start(bool extend) { set_caret(line_start(caret_), extend); }
void Input::move_line_end(bool extend) { set_caret(line_end(caret_), extend); }

bool Input::move_up(bool extend) {
  ensure();
  const CellPos p = cell_of(caret_);
  if (p.row == 0) return false;
  const int goal = goal_col_.value_or(p.col);
  place(pos_at(p.row - 1, goal), extend);
  goal_col_ = goal;
  return true;
}

bool Input::move_down(bool extend) {
  ensure();
  const CellPos p = cell_of(caret_);
  if (p.row >= flow_.rows - 1) return false;
  const int goal = goal_col_.value_or(p.col);
  place(pos_at(p.row + 1, goal), extend);
  goal_col_ = goal;
  return true;
}

// ---- history -------------------------------------------------------------------------

void Input::push_history(std::string entry) {
  if (!entry.empty() && (hist_.empty() || hist_.back() != entry)) {
    hist_.push_back(std::move(entry));
    while (opt_.history_limit > 0 && hist_.size() > opt_.history_limit) hist_.erase(hist_.begin());
  }
  hist_pos_ = hist_.size();
  draft_.clear();
}

bool Input::history_prev() {
  if (hist_pos_ == 0 || hist_.empty()) return false;
  if (hist_pos_ >= hist_.size()) {
    hist_pos_ = hist_.size();
    draft_ = text_;
  }
  --hist_pos_;
  set_text(hist_[hist_pos_]);
  return true;
}

bool Input::history_next() {
  if (hist_pos_ >= hist_.size()) return false;
  ++hist_pos_;
  set_text(hist_pos_ == hist_.size() ? draft_ : hist_[hist_pos_]);
  return true;
}

// ---- layout --------------------------------------------------------------------------

void Input::set_options(const InputOptions& o) {
  const bool retab = o.ambiguous_wide != opt_.ambiguous_wide;
  opt_ = o;
  prompt_w_ = unicode::display_width(opt_.prompt, opt_.ambiguous_wide);
  if (retab) g_ = unicode::graphemes(text_, opt_.ambiguous_wide);
  dirty_ = true;
}

Input::Flow Input::flow(int width) const {
  Flow f;
  f.cells.assign(g_.size(), {});
  const int indent = prompt_w_;
  const int cap = std::max(width - 2 * opt_.inset, indent + 1);
  const int tab = std::max(opt_.tab_width, 1);
  int row = 0, col = indent;
  for (std::size_t i = 0; i < g_.size(); ++i) {
    const unicode::Grapheme& g = g_[i];
    const char c0 = text_[g.offset];
    if (c0 == '\n') {
      f.cells[i] = {row, col, 0};
      f.row_end.push_back(g.offset);
      ++row;
      col = indent;
      continue;
    }
    int w = c0 == '\t' ? tab - ((col - indent) % tab) : g.width;
    if (w > 0 && col + w > cap && col > indent) {
      f.row_end.push_back(g.offset);
      ++row;
      col = indent;
      if (c0 == '\t') w = tab;
    }
    f.cells[i] = {row, col, w};
    col += w;
  }
  if (col >= cap && col > indent) {  // a full row: the end of the text starts the next
    f.row_end.push_back(text_.size());
    ++row;
    col = indent;
  }
  f.row_end.push_back(text_.size());
  f.end_row = row;
  f.end_col = col;
  f.rows = row + 1;
  return f;
}

void Input::ensure() const {
  if (!dirty_) return;
  flow_ = flow(width_);
  dirty_ = false;
}

int Input::rows_for(int width) const {
  if (width == width_) {
    ensure();
    return flow_.rows;
  }
  return flow(width).rows;
}

int Input::rows() const {
  ensure();
  return flow_.rows;
}

void Input::layout(Rect area) {
  area_ = {area.x + opt_.inset, area.y, std::max(area.w - 2 * opt_.inset, 0), area.h};
  if (area.w != width_) {
    width_ = area.w;
    dirty_ = true;
  }
  ensure();
  const int h = std::max(area_.h, 1);
  const int cr = cell_of(caret_).row;
  if (cr < top_) top_ = cr;
  if (cr >= top_ + h) top_ = cr - h + 1;
  top_ = std::clamp(top_, 0, std::max(flow_.rows - h, 0));
}

Input::CellPos Input::cell_of(std::size_t offset) const {
  ensure();
  offset = snap(offset);
  if (offset >= text_.size()) return {flow_.end_row, flow_.end_col};
  auto it = std::lower_bound(g_.begin(), g_.end(), offset,
                             [](const unicode::Grapheme& g, std::size_t p) { return g.offset < p; });
  const std::size_t i = static_cast<std::size_t>(it - g_.begin());
  return {flow_.cells[i].row, flow_.cells[i].col};
}

// The position on `row` at column `col`: the grapheme covering it, the first
// grapheme right of it (a column over the prompt / indent), else the row's end.
std::size_t Input::pos_at(int row, int col) const {
  ensure();
  row = std::clamp(row, 0, flow_.rows - 1);
  for (std::size_t i = 0; i < g_.size(); ++i) {
    const Cell& c = flow_.cells[i];
    if (c.row != row) continue;
    if (col < c.col) return g_[i].offset;
    if (col < c.col + std::max(c.width, 1)) return g_[i].offset;
  }
  return flow_.row_end[static_cast<std::size_t>(row)];
}

std::optional<Input::Hit> Input::hit(int x, int y) const {
  if (area_.w <= 0 && area_.h <= 0) return std::nullopt;
  ensure();
  const int row = std::clamp(top_ + (y - area_.y), 0, flow_.rows - 1);
  const int col = x - area_.x;
  for (std::size_t i = 0; i < g_.size(); ++i) {
    const Cell& c = flow_.cells[i];
    if (c.row != row) continue;
    const bool newline = text_[g_[i].offset] == '\n';
    if (c.width == 0 && !newline) continue;  // draws nothing, so it is never "under" a pointer
    if (col < c.col || (!newline && col < c.col + std::max(c.width, 1))) {
      if (newline || col < c.col) return Hit{g_[i].offset, g_[i].offset};
      return Hit{g_[i].offset, g_[i].offset + g_[i].length};
    }
    if (newline) break;
  }
  const std::size_t e = flow_.row_end[static_cast<std::size_t>(row)];
  return Hit{e, e};
}

void Input::draw(Frame& f, const Theme& theme, bool focused) const {
  ensure();
  const Style prompt = theme.style(opt_.prompt_role);
  const Style txt = theme.style(Role::input_text);
  const Style sel = theme.style(Role::selection);
  const Style ph = theme.style(Role::input_placeholder);
  const Rect a = area_;
  const int h = std::max(a.h, 0);
  auto visible = [&](int row) { return row >= top_ && row < top_ + h; };
  if (visible(0)) f.put_text(a.x, a.y, opt_.prompt, prompt, std::max(a.w, 0), opt_.ambiguous_wide);
  if (text_.empty()) {
    if (visible(0) && !opt_.placeholder.empty())
      f.put_text(a.x + prompt_w_, a.y, opt_.placeholder, ph, std::max(a.w - prompt_w_, 0), opt_.ambiguous_wide);
  }
  const bool has_sel = !sel_.empty();
  const std::size_t sb = sel_.begin(), se = sel_.end();
  for (std::size_t i = 0; i < g_.size(); ++i) {
    const Cell& c = flow_.cells[i];
    if (!visible(c.row)) continue;
    const int y = a.y + (c.row - top_);
    const int x = a.x + c.col;
    const std::size_t off = g_[i].offset;
    const bool in_sel = has_sel && off >= sb && off < se;
    const Style& st = in_sel ? sel : txt;
    const char c0 = text_[off];
    if (c0 == '\n') {  // a selected newline shows as one highlighted cell
      if (in_sel && c.col < a.w) f.put(x, y, " ", 1, st);
      continue;
    }
    if (c0 == '\t') {
      for (int k = 0; k < c.width && c.col + k < a.w; ++k) f.put(x + k, y, " ", 1, st);
      continue;
    }
    if (c.width <= 0 || c.col + c.width > a.w) continue;  // width-0: nothing to draw; clipped: the area's edge
    f.put(x, y, std::string_view(text_).substr(off, g_[i].length), c.width, st);
  }
  if (focused) {
    const CellPos p = cell_of(caret_);
    if (visible(p.row)) f.set_cursor(a.x + std::min(p.col, std::max(a.w - 1, 0)), a.y + (p.row - top_), true);
  }
}

// ---- events --------------------------------------------------------------------------

void Input::unit_around(std::size_t off, bool word, std::size_t& b, std::size_t& e) const {
  off = std::min(off, text_.size());
  if (word) {
    const unicode::ByteRange r = unicode::word_range(text_, off);
    b = r.begin;
    e = r.end;
    return;
  }
  b = line_start(off);
  e = line_end(off);
}

InputAction Input::handle(const Event& e, std::uint64_t now_ms) {
  if (const PasteEvent* p = std::get_if<PasteEvent>(&e)) {
    insert(p->text);
    return InputAction::Handled;
  }
  if (const MouseEvent* m = std::get_if<MouseEvent>(&e)) return handle_mouse(*m, now_ms);
  if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) return handle_key(*k);
  return InputAction::Ignored;
}

InputAction Input::handle_key(const KeyEvent& k) {
  using A = InputAction;
  const bool sh = k.shift, word = k.ctrl || k.alt;
  switch (k.key) {
    case Key::Enter:
      if (k.alt && !k.ctrl) { insert("\n"); return A::Handled; }
      return A::Submit;
    case Key::Backspace:
      if (word) kill_word_backward(); else erase_backward();
      return A::Handled;
    case Key::Delete:
      if (word) kill_word_forward(); else erase_forward();
      return A::Handled;
    case Key::Left:
      if (word) move_word_left(sh); else move_left(sh);
      return A::Handled;
    case Key::Right:
      if (word) move_word_right(sh); else move_right(sh);
      return A::Handled;
    case Key::Home:
      if (k.ctrl || k.alt || text_.empty()) return A::Ignored;
      move_line_start(sh);
      return A::Handled;
    case Key::End:
      if (k.ctrl || k.alt || text_.empty()) return A::Ignored;
      move_line_end(sh);
      return A::Handled;
    case Key::Up:
      if (k.ctrl || k.alt) return A::Ignored;
      if (sh) { move_up(true); return A::Handled; }
      if (!move_up(false)) history_prev();
      return A::Handled;
    case Key::Down:
      if (k.ctrl || k.alt) return A::Ignored;
      if (sh) { move_down(true); return A::Handled; }
      if (!move_down(false)) history_next();
      return A::Handled;
    case Key::Escape:
      if (k.ctrl || k.alt || sel_.empty()) return A::Ignored;
      clear_selection();
      return A::Handled;
    case Key::Char:
      break;
    default:
      return A::Ignored;
  }
  if (k.ctrl && !k.alt) {
    switch (k.ch) {
      case 'a': select_all(); return A::Handled;
      case 'u': kill_to_line_start(); return A::Handled;
      case 'k': kill_to_line_end(); return A::Handled;
      case 'w': kill_word_backward(); return A::Handled;
      case 'd':
        if (text_.empty()) return A::Eof;
        erase_forward();
        return A::Handled;
      default: return A::Ignored;
    }
  }
  if (k.alt && !k.ctrl) {
    switch (k.ch) {
      case 'c':
        if (sel_.empty()) return A::Ignored;
        if (on_copy) on_copy(selected_text());
        return A::Handled;
      case 'd': kill_word_forward(); return A::Handled;
      default: return A::Ignored;
    }
  }
  if (k.ctrl || k.alt) return A::Ignored;
  if (k.ch < 0x20 || k.ch == 0x7F) return A::Ignored;
  std::string s;
  unicode::append_utf8(s, k.ch);
  insert(s);
  return A::Handled;
}

InputAction Input::handle_mouse(const MouseEvent& m, std::uint64_t now_ms) {
  using A = InputAction;
  using K = MouseEvent::Kind;
  switch (m.kind) {
    case K::Press: {
      if (m.button != 1) return A::Ignored;
      const std::optional<Hit> h = hit(m.x, m.y);
      if (!h) return A::Ignored;
      goal_col_.reset();
      if (m.shift) {  // extend from the anchor (or the caret), the pointer's glyph included
        const std::size_t anchor = sel_.active ? sel_.anchor : caret_;
        place(h->begin >= anchor ? h->end : h->begin, true);
        click_ = {};
        drag_ = {true, anchor, anchor, m.x, m.y};
        return A::Handled;
      }
      const bool paired = click_.count > 0 && now_ms >= click_.at_ms && now_ms - click_.at_ms <= opt_.multi_click_ms &&
                          std::abs(m.x - click_.x) <= 1 && m.y == click_.y;
      click_.count = paired ? click_.count + 1 : 1;
      if (click_.count > 3) click_.count = 1;
      click_.at_ms = now_ms;
      click_.x = m.x;
      click_.y = m.y;
      std::size_t b = h->begin, e = h->end;
      if (click_.count >= 2) unit_around(h->begin, click_.count == 2, b, e);
      drag_ = {true, b, e, m.x, m.y};
      if (click_.count >= 2 && b != e) {
        sel_ = {b, e, true};
        caret_ = e;
      } else {
        sel_ = {};
        caret_ = b;
      }
      return A::Handled;
    }
    case K::Drag:
      if (!drag_.active) return A::Ignored;
      drag_to(m.x, m.y);
      return A::Handled;
    case K::Release: {
      if (!drag_.active) return A::Ignored;
      // A release where the pointer already is changes nothing (a double-click's word
      // is not narrowed to the cell under the button).
      if (m.x != drag_.x || m.y != drag_.y) drag_to(m.x, m.y);
      drag_.active = false;
      if (sel_.empty()) {
        sel_ = {};
        return A::Handled;
      }
      if (on_copy) on_copy(selected_text());
      return A::Handled;
    }
    default:
      return A::Ignored;
  }
}

void Input::drag_to(int x, int y) {
  drag_.x = x;
  drag_.y = y;
  const std::optional<Hit> h = hit(x, y);
  if (!h) return;
  std::size_t b = h->begin, e = h->end;
  if (click_.count >= 2) unit_around(h->begin, click_.count == 2, b, e);
  if (b < drag_.begin) {
    sel_.anchor = drag_.end;
    sel_.head = b;
  } else {
    sel_.anchor = drag_.begin;
    sel_.head = std::max(e, drag_.end);
  }
  sel_.active = true;
  caret_ = sel_.head;
}

}  // namespace rolltui
