// rolltui/Widgets.cpp — see Widgets.hpp. Every concrete widget lives here: a host
// reaches them through Windows, so the set of kinds is one table (Layout.hpp) and one
// factory (widget_for), never a chain of names in an application.
#include "rolltui/Widgets.hpp"

#include "rolltui/Scratch.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <variant>

#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"

namespace rolltui {

std::string WindowsReport::summary() const {
  if (bad_values.empty()) return {};
  std::string s = bad_values.front();
  if (bad_values.size() > 1) s += " (+" + std::to_string(bad_values.size() - 1) + " more)";
  return s;
}

Rect content_rect(const ResolvedNode& rn) {
  Rect r = rn.inner;
  if (rn.node->border != Border::None && r.w >= 3) {
    r.x += 1;
    r.w -= 2;
  }
  return r;
}

// ---- the scrollbar's geometry (Widgets.hpp) -----------------------------------------

bool scroll_thumb(const Widget::ScrollExtent& e, int track, ScrollThumb& out) {
  // No bar when there is nothing to scroll, or nowhere to draw one. Both are answers,
  // not edge cases: a bar on a document that fits is a lie about there being more.
  if (track <= 0 || e.total == 0 || e.visible == 0 || e.total <= e.visible) return false;
  const double frac = static_cast<double>(e.visible) / static_cast<double>(e.total);
  int len = static_cast<int>(frac * track + 0.5);
  if (len < 1) len = 1;          // always visible: a 1-cell thumb still says where you are
  if (len > track) len = track;
  const std::size_t max_first = e.total - e.visible;
  const std::size_t first = e.first > max_first ? max_first : e.first;
  const int span = track - len;  // the cells the thumb can travel
  int off = span <= 0 ? 0 : static_cast<int>(static_cast<double>(first) / static_cast<double>(max_first) * span + 0.5);
  if (off < 0) off = 0;
  if (off > span) off = span;
  // THE GUARANTEE: the thumb touches an end IF AND ONLY IF the view is at that end.
  // Snapping the ends is not enough on its own — with 90 positions and 9 travel cells,
  // line 89 also rounds onto the last cell, so "the thumb is at the bottom" would stop
  // meaning "you are at the bottom" and a reader could not tell one line short of the
  // end from the end. So the end cells are RESERVED for the ends and everything between
  // is squeezed into what is left. Below a 2-cell span there is nothing to reserve, and
  // the honest answer is the ends alone.
  if (span >= 2) {
    if (first == 0) off = 0;
    else if (first == max_first) off = span;
    else off = std::clamp(off, 1, span - 1);
  } else {
    off = (first == max_first) ? span : 0;
  }
  out.offset = off;
  out.length = len;
  return true;
}

std::size_t scroll_first_for_cell(const Widget::ScrollExtent& e, int track, int cell) {
  if (e.total <= e.visible || track <= 0) return 0;
  const std::size_t max_first = e.total - e.visible;
  ScrollThumb t;
  const int len = scroll_thumb(e, track, t) ? t.length : 1;
  const int span = track - len;
  if (span <= 0) return cell <= 0 ? 0 : max_first;
  int c = cell;
  if (c < 0) c = 0;
  if (c > span) c = span;
  const double f = static_cast<double>(c) / static_cast<double>(span) * static_cast<double>(max_first) + 0.5;
  const std::size_t first = static_cast<std::size_t>(f);
  return first > max_first ? max_first : first;
}

int draw_scrolled_text(const ResolvedNode& rn, Frame& f, const Theme& theme, std::string_view text, int top,
                       bool ambiguous_wide) {
  const Rect r = content_rect(rn);
  WrapOptions wo;
  wo.ambiguous_wide = ambiguous_wide;
  const std::vector<Line> lines = wrap(text, std::max(r.w, 1), wo);
  const int total = static_cast<int>(lines.size());
  if (r.w <= 0 || r.h <= 0) return total;
  int y = r.y;
  for (std::size_t i = static_cast<std::size_t>(std::max(top, 0)); i < lines.size() && y < r.y + r.h; ++i)
    f.put_text(r.x + lines[i].indent, y++, lines[i].text, theme.style(Role::text), std::max(r.w - lines[i].indent, 0),
               ambiguous_wide);
  const int below = total - std::max(top, 0) - r.h;
  const std::string marker = scroll_marker_text(below > 0 ? static_cast<std::size_t>(below) : 0, r.w, ambiguous_wide);
  if (!marker.empty()) {
    const int mw = unicode::display_width(marker, ambiguous_wide);
    f.put_text(r.x + std::max(r.w - mw, 0), r.y + r.h - 1, marker, theme.style(Role::scroll_marker), mw, ambiguous_wide);
  }
  return total;
}

std::string help_document(const Bindings& b, std::string_view lead, const std::vector<std::string>& scopes,
                         std::string_view note) {
  std::string out(lead);
  for (const std::string& scope : scopes) {
    out += scope + ":\n";
    for (const std::string& line : help_lines(b, scope)) out += "  " + line + "\n";
  }
  return out + std::string(note);
}

int input_max_rows(int parent_extent, int border_rows) { return std::max(1, parent_extent / 2 - border_rows); }

int input_rows(int text_rows, int end_col, int note_width, int width, int max_rows) {
  max_rows = std::max(max_rows, 1);
  const int rows = std::clamp(text_rows, 1, max_rows);
  if (note_width <= 0) return rows;
  if (rows == 1 && end_col + 2 + note_width <= width) return 1;
  return std::min(rows + 1, max_rows);
}

bool scroll_by_action(const KeyEvent& k, const Bindings& b, int page, int total, int& top) {
  page = std::max(page, 1);
  const std::string_view a = b.action_for(k, "transcript");
  if (a == "transcript.line_up") top -= 1;
  else if (a == "transcript.line_down") top += 1;
  else if (a == "transcript.page_up") top -= page;
  else if (a == "transcript.page_down") top += page;
  else if (a == "transcript.top") top = 0;
  else if (a == "transcript.bottom") top = total;
  else return false;
  top = std::clamp(top, 0, std::max(total - page, 0));
  return true;
}

namespace {

std::string read_text_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// A file's identity for change detection, and -1 when it is not there: modification
// time to the NANOSECOND, plus the size. Whole seconds alone miss a rewrite inside one
// second — the same granularity trap that makes cmake skip a rebuild and run a suite
// against a stale binary (CLAUDE.md), and a menu or a `file:` window edited twice in a
// second is exactly what someone iterating on one does.
long long file_stamp(const std::string& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) return -1;
  return static_cast<long long>(st.st_mtimespec.tv_sec) * 1000000000LL + st.st_mtimespec.tv_nsec + static_cast<long long>(st.st_size);
}

}  // namespace

// ---- the widgets ---------------------------------------------------------------------

// Named (not in the anonymous namespace) because it is Windows' one friend: every
// widget reaches the host's bindings through these accessors and nothing else does.
class WidgetBase : public Widget {
 public:
  explicit WidgetBase(Windows& w) : w_(&w) {}

 protected:
  const WidgetEnv& env() const { return w_->env(); }
  bool amb() const { return w_->env().ambiguous_wide; }
  const Bindings& binds() const { return w_->bindings(); }
  const Document* document(const std::string& name) const {
    auto it = w_->documents_.find(name);
    return it == w_->documents_.end() ? nullptr : it->second;
  }
  const Windows::RowsFn* rows_fn(const std::string& name) const {
    auto it = w_->rows_.find(name);
    return it == w_->rows_.end() ? nullptr : &it->second;
  }
  const Windows::SubmitFn* submit_fn(const std::string& name) const {
    auto it = w_->submits_.find(name);
    return it == w_->submits_.end() ? nullptr : &it->second;
  }
  const Windows::NoteFn* note_fn(const std::string& name) const {
    auto it = w_->notes_.find(name);
    return it == w_->notes_.end() ? nullptr : &it->second;
  }
  const std::string* host_menu(const std::string& name) const {
    auto it = w_->host_menus_.find(name);
    return it == w_->host_menus_.end() ? nullptr : &it->second;
  }
  const std::string& dir() const { return w_->dir_; }
  std::string unbound() const { return "nothing is bound to '" + content.source + "'"; }

  Windows* w_;
};

namespace {

// The window that could not be understood: the reason, in the error role, wrapped.
// Never blank — a layout mistake has to look like one.
class ErrorWidget : public WidgetBase {
 public:
  ErrorWidget(Windows& w, std::string why) : WidgetBase(w), why_(std::move(why)) {}
  std::string problem() const override { return why_; }
  void layout(const ResolvedNode&) override {}
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    const Rect r = content_rect(rn);
    WrapOptions wo;
    wo.ambiguous_wide = amb();
    int y = r.y;
    for (const Line& l : wrap("[" + why_ + "]", std::max(r.w, 1), wo)) {
      if (y >= r.y + r.h) break;
      f.put_text(r.x + l.indent, y++, l.text, theme.style(Role::error), std::max(r.w - l.indent, 0), amb());
    }
  }

 private:
  std::string why_;
};

// transcript:<document>
class TranscriptWidget : public WidgetBase {
 public:
  using WidgetBase::WidgetBase;
  Transcript t;
  std::uint64_t highlighter_seen_ = 0;

  std::string problem() const override { return document(content.source) ? std::string() : unbound(); }
  void layout(const ResolvedNode& rn) override {
    sync_highlighter();
    if (const Document* d = document(content.source)) t.layout(*d, rn.inner, options(rn));
  }
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    const Document* d = document(content.source);
    if (!d) return;
    sync_highlighter();
    t.layout(*d, rn.inner, options(rn));
    t.draw(f, theme);
  }
  // set_highlight() bumps the transcript's own cache epoch, so handing it over on every
  // frame would re-lay the whole transcript on every frame. Track the HOST's epoch
  // instead: this picks up a highlighter set at any time, and picks it up once.
  void sync_highlighter() {
    if (highlighter_seen_ == w_->highlighter_epoch()) return;
    highlighter_seen_ = w_->highlighter_epoch();
    t.set_highlight(w_->highlighter());
  }
  bool handle(const Event& e) override {
    const Document* d = document(content.source);
    return d && t.handle(e, *d, env().now_ms, binds());
  }
  // The window never learns that this position is really an ANCHOR (entry, line within
  // it): it asks in lines and commands in lines, and the widget converts. That is the
  // whole reason the window is forbidden to store the number — a re-wrap changes the
  // total and the meaning of the offset at the same instant.
  std::optional<ScrollExtent> scroll_extent(Axis axis) const override {
    if (axis != Axis::Vertical) return std::nullopt;
    return ScrollExtent{t.top_line(), static_cast<std::size_t>(std::max(t.viewport_height(), 0)), t.total_lines()};
  }
  bool scroll_to(Axis axis, std::size_t first) override {
    if (axis != Axis::Vertical) return false;
    t.scroll_by(static_cast<long>(first) - static_cast<long>(t.top_line()));
    return true;
  }

 private:
  TranscriptOptions options(const ResolvedNode& rn) const {
    TranscriptOptions o;
    o.ambiguous_wide = amb();
    o.inset = rn.node->border != Border::None ? 1 : 0;
    o.code_fold_over_lines = w_->code_fold_over_lines();
    o.code_cap_lines = w_->code_cap_lines();
    return o;
  }
};

// input:<target> — the line editor, which GROWS its window with its text (to half the
// parent's height) and may carry a one-line note beside or under the prompt.
class InputWidget : public WidgetBase {
 public:
  using WidgetBase::WidgetBase;
  Input ed;
  int min_outer = 0;  // a host's floor (roll holds it as tall as the modal above it)

  std::string problem() const override { return submit_fn(content.source) ? std::string() : unbound(); }

  std::optional<int> desired_outer(int inner_w, int parent_extent, int border) const override {
    max_rows_ = input_max_rows(parent_extent, border);
    return std::max(rows_with_note(inner_w, ed.rows_for(inner_w)) + border, min_outer);
  }
  void layout(const ResolvedNode& rn) override {
    InputOptions o = ed.options();
    o.ambiguous_wide = amb();
    o.inset = rn.node->border != Border::None ? 1 : 0;  // the widget owns the breathing room
    if (!(o == ed.options())) ed.set_options(o);
    ed.layout(text_rect(rn.inner));
  }
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    const Rect r = rn.inner;
    const Rect tr = text_rect(r);
    ed.layout(tr);
    ed.draw(f, theme, rn.focused);
    const Note& note = note_info();
    if (note.text.empty()) return;
    const int nw = unicode::display_width(note.text, amb());
    // Phase 12 m6: the note is DRAWN here and MARKED here, over exactly the cells it took
    // — never a rectangle, and never the row it happens to sit on, so a note that shares
    // its row with the prompt cannot animate the prompt.
    if (tr.h < r.h) {  // its own row, under the text
      const int used = f.put_text(r.x, r.y + tr.h, note.text, theme.style(Role::text_muted), std::max(r.w, 0), amb());
      f.mark(r.x, r.y + tr.h, used, note.state, note.since_ms);
      return;
    }
    const int nx = std::max(r.x + r.w - nw, r.x + end_col() + 2);
    const int used = f.put_text(nx, r.y, note.text, theme.style(Role::text_muted), std::max(r.x + r.w - nx, 0), amb());
    f.mark(nx, r.y, used, note.state, note.since_ms);
  }
  // A layout-declared input with no host code still edits and submits; a host that
  // wants the action back (to quit on Eof, to offer an Ignored key elsewhere) calls
  // event() through Windows::input_event instead.
  InputAction event(const Event& e) {
    const InputAction a = ed.handle(e, binds(), env().now_ms);
    if (a != InputAction::Submit) return a;
    std::string text = ed.text();
    // A prompt sends and starts fresh; a find bar keeps its standing query (Widgets.hpp).
    if (w_->on_submit_for(content.source) == Windows::OnSubmit::SendAndClear) {
      ed.push_history(text);
      ed.clear();
    }
    if (const Windows::SubmitFn* fn = submit_fn(content.source); fn && *fn) (*fn)(text);
    return a;
  }
  bool handle(const Event& e) override { return event(e) != InputAction::Ignored; }

 private:
  // m5b: the Note is a MEMBER the host refills, not one it returns. This is called
  // several times a frame (sizing, then drawing), so a by-value return was several
  // allocations per frame for a line that rarely changes.
  const Note& note_info() const {
    note_.text.clear();
    note_.state = EffectState::None;
    note_.since_ms = 0;
    if (const Windows::NoteFn* fn = note_fn(content.source); fn && *fn) (*fn)(note_);
    return note_;
  }
  mutable Note note_;
  // The column just past the text (or past the placeholder while it is empty): where a
  // note may sit on the first row.
  int end_col() const {
    if (!ed.text().empty()) return ed.cell_of(ed.text().size()).col;
    return unicode::display_width(ed.options().prompt, amb()) + unicode::display_width(ed.options().placeholder, amb());
  }
  // Rows of window text: the text's rows, capped at half the parent, plus one for the
  // note when it does not fit beside a single row.
  int rows_with_note(int width, int text_rows) const {
    const std::string& note = note_info().text;
    return input_rows(text_rows, end_col(), note.empty() ? 0 : unicode::display_width(note, amb()), width, max_rows_);
  }
  // The note takes a row of its own exactly when the window has more than one: with a
  // single row it sits beside the text (and rows_with_note only ever returns 1 with a
  // note when it fits there, or when half the parent leaves no room for a second row).
  bool note_owns_row(int width) const {
    return !note_info().text.empty() && rows_with_note(width, ed.rows_for(width)) > 1;
  }
  Rect text_rect(Rect r) const {
    if (r.h > 1 && note_owns_row(r.w)) r.h -= 1;
    return r;
  }

  mutable int max_rows_ = 1;  // this frame's cap, from the last desired_outer()
};

// menu:<name> — a menu FILE (Phase 10 m3). The three rungs are in Widgets.hpp; the
// widget OWNS the Menu built from whichever answered, and the host reads it back
// through Windows::menu() to fill a choice's options and to act on its events.
//
// A user's file is re-read when it changes on disk, exactly like `file:` — one rule for
// both, and it is what makes "drop a menu in and open it" true without a restart. The
// cost of a re-read is the navigation position and any options a host has filled in;
// both hosts refill on opening the menu, so it is a menu that jumps back to its top
// level the frame after its file changes, which is what someone editing it wants.
class MenuWidget : public WidgetBase {
 public:
  using WidgetBase::WidgetBase;

  Menu& menu() {
    refresh();
    return m_;
  }
  std::string origin() const {
    refresh();
    return origin_;
  }
  std::string problem() const override {
    refresh();
    return problem_;
  }
  // The file's own problems, plus the ones only the LIVE table can see: an item naming
  // an action no layout declares. Recomputed every call rather than cached with the
  // file, because the bindings and the layout change under a menu that has not.
  std::vector<std::string> notes() const override {
    refresh();
    std::vector<std::string> out = notes_;
    for (const auto& [id, action] : m_.item_actions())
      if (!binds().has(action))
        out.push_back("menu file (" + origin_ + "): item '" + id + "' names the action '" + action +
                      "', which no layout declares");
    return out;
  }
  // Reports only — see Menu.hpp. There is deliberately no scroll_to override.
  std::optional<ScrollExtent> scroll_extent(Axis axis) const override {
    if (axis != Axis::Vertical) return std::nullopt;
    refresh();
    return ScrollExtent{m_.scroll_first(), m_.scroll_visible(), m_.scroll_total()};
  }
  void layout(const ResolvedNode& rn) override {
    refresh();
    m_.apply_shortcuts(binds());  // an item's chords, this frame — never a string in the file
    MenuOptions o = m_.options();
    o.ambiguous_wide = amb();
    o.inset = rn.node->border != Border::None ? 1 : 0;
    m_.set_options(o);
    m_.layout(rn.inner);
  }
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    layout(rn);
    m_.draw(f, theme, rn.focused);
  }

 private:
  std::string user_path() const {
    return dir().empty() ? std::string() : dir() + "/menus/" + content.source + ".json";
  }
  // Which rung answers, and with what — the order stated in Widgets.hpp. A file the
  // user has is preferred even when it is unreadable garbage: shadowing must not fail
  // over to a different menu, or a typo in one's own file is a silent substitution.
  void resolve(std::string& text, std::string& origin, long long& stamp) const {
    const std::string p = user_path();
    stamp = p.empty() ? -1 : file_stamp(p);
    if (stamp >= 0) {
      bool ok = false;
      text = read_text_file(p, ok);
      origin = p;
      return;
    }
    if (const std::string* host = host_menu(content.source)) {
      text = *host;
      origin = "the host's";
      return;
    }
    if (const std::string_view shipped = shipped_menu(content.source); !shipped.empty()) {
      text = std::string(shipped);
      origin = "a shipped menu";
      return;
    }
    text.clear();
    origin.clear();
  }
  void refresh() const {
    std::string text, origin;
    long long stamp = -1;
    resolve(text, origin, stamp);
    if (loaded_ && origin == origin_ && stamp == stamp_) return;
    loaded_ = true;
    origin_ = origin;
    stamp_ = stamp;
    notes_.clear();
    if (origin.empty()) {
      m_.set_root(MenuItem::submenu(content.source, content.source, {}));
      problem_ = "no menu file '" + content.source + "' (looked for " +
                 (user_path().empty() ? "menus/" + content.source + ".json under a preset directory (none set)" : "'" + user_path() + "'") +
                 ", the host's menus and the shipped ones)";
      return;
    }
    MenuLoadReport rep;
    std::optional<MenuItem> root = menu_from_json(text, rep);
    if (!root) {
      m_.set_root(MenuItem::submenu(content.source, content.source, {}));
      problem_ = "menu file (" + origin + ") is unusable: " + rep.error;
      return;
    }
    problem_.clear();
    for (const std::vector<std::string>* list : {&rep.unknown_keys, &rep.bad_values})
      for (const std::string& s : *list) notes_.push_back("menu file (" + origin + "): " + s);
    m_.set_root(std::move(*root));
  }

  mutable Menu m_;
  mutable bool loaded_ = false;
  mutable long long stamp_ = -1;
  mutable std::string origin_, problem_;
  mutable std::vector<std::string> notes_;
};

// rows:<source> — label/value rows a host supplies. One line when the window is one
// row high (the stacked layout's strip); otherwise a label column of 8 cells with the
// value wrapping onto following rows, so a long value pushes the next row down rather
// than hiding it.
class RowsWidget : public WidgetBase {
 public:
  using WidgetBase::WidgetBase;
  std::string problem() const override { return rows_fn(content.source) ? std::string() : unbound(); }
  // m5b: the rows and the one-row line are MEMBERS, so a frame refills storage that is
  // already there instead of building and destroying it.
  mutable Rows rows_;
  mutable std::string line_;
  void layout(const ResolvedNode&) override {}
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    const Windows::RowsFn* fn = rows_fn(content.source);
    if (!fn || !*fn) return;
    rows_.reset();   // m5b: keeps the storage; the host refills it in place
    (*fn)(rows_);
    const Rows& rows = rows_;
    const Rect r = rn.inner;
    const Style label = theme.style(Role::label), value = theme.style(Role::value);
    if (r.h == 1) {
      line_.clear();  // m5b: a member, so the one-row form reuses its buffer too
      std::string& s = line_;
      for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!s.empty()) s += "  ";
        s += rows[i].label;
        s += ' ';
        s += rows[i].value;
      }
      f.put_text(r.x + 1, r.y, s, value, std::max(r.w - 1, 0), amb());
      return;
    }
    WrapOptions wo;
    wo.ambiguous_wide = amb();
    int y = r.y;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const Row& row = rows[i];
      if (y >= r.y + r.h) break;
      f.put_text(r.x + 1, y, row.label, label, std::max(r.w - 1, 0), amb());
      auto borrowed = wrap_borrow(row.value, std::max(r.w - 9, 1), wo);  // m5b: lent, not built
      const WrapLines& lines = *borrowed;
      if (lines.empty()) { ++y; continue; }  // an empty value still takes its row
      for (const Line& l : lines) {
        if (y >= r.y + r.h) break;
        f.put_text(r.x + 9, y, l.text, value, std::max(r.w - 9, 0), amb());
        ++y;
      }
    }
  }
};

// text: / file: / help — wrapped text that scrolls by the transcript scope's actions,
// with the transcript's own "▼ N more" marker for what is below.
class ScrollTextWidget : public WidgetBase {
 public:
  using WidgetBase::WidgetBase;
  virtual std::string text() const = 0;

  void layout(const ResolvedNode& rn) override {
    area_ = content_rect(rn);
    WrapOptions wo;
    wo.ambiguous_wide = amb();
    total_ = static_cast<int>(wrap(text(), std::max(area_.w, 1), wo).size());
    top_ = std::clamp(top_, 0, std::max(total_ - std::max(area_.h, 1), 0));
  }
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    layout(rn);
    total_ = draw_scrolled_text(rn, f, theme, text(), top_, amb());
  }
  bool handle(const Event& e) override {
    const KeyEvent* k = std::get_if<KeyEvent>(&e);
    return k && scroll_by_action(*k, binds(), area_.h, total_, top_);
  }
  // Reports AND accepts: a wrapped-text view's position really is a line number, so
  // there is nothing richer for the window to lose by driving it.
  std::optional<ScrollExtent> scroll_extent(Axis axis) const override {
    if (axis != Axis::Vertical) return std::nullopt;
    return ScrollExtent{static_cast<std::size_t>(std::max(top_, 0)), static_cast<std::size_t>(std::max(area_.h, 0)),
                        static_cast<std::size_t>(std::max(total_, 0))};
  }
  bool scroll_to(Axis axis, std::size_t first) override {
    if (axis != Axis::Vertical) return false;
    top_ = std::clamp(static_cast<int>(first), 0, std::max(total_ - std::max(area_.h, 1), 0));
    return true;
  }

 private:
  Rect area_;
  int top_ = 0, total_ = 0;
};

// text:<literal> — the layout file's own words.
class TextWidget : public ScrollTextWidget {
 public:
  using ScrollTextWidget::ScrollTextWidget;
  std::string text() const override { return content.source; }
};

// file:<path> — re-read when the file's mtime changes, so a dropped-in file shows up.
class FileWidget : public ScrollTextWidget {
 public:
  using ScrollTextWidget::ScrollTextWidget;
  std::string problem() const override {
    refresh();
    return ok_ ? std::string() : "cannot read '" + path() + "'";
  }
  std::string text() const override {
    refresh();
    return ok_ ? body_ : std::string();
  }

 private:
  std::string path() const {
    const std::string& p = content.source;
    if (!p.empty() && p[0] == '/') return p;
    return dir().empty() ? p : dir() + "/" + p;
  }
  void refresh() const {
    const std::string p = path();
    const long long m = file_stamp(p);
    if (read_ && m == stamp_ && p == read_path_) return;
    read_ = true;
    stamp_ = m;
    read_path_ = p;
    body_ = read_text_file(p, ok_);
  }
  mutable bool read_ = false, ok_ = false;
  mutable long long stamp_ = -1;
  mutable std::string read_path_, body_;
};

// help — the key list, rendered from the LIVE bindings, so it cannot lie about a
// rebinding. The lead and note lines are the host's (set_help), and so is the SET of
// scopes an app has — but WHICH of them a window lists is the LAYOUT's since Phase 11
// m5b: `help` is all of them, `help:app` is that one. A scope the host does not have is
// a named problem and an error panel, like every other unbound source; it is not an
// empty window, which is what silently ignoring it would produce.
class HelpWidget : public ScrollTextWidget {
 public:
  using ScrollTextWidget::ScrollTextWidget;
  std::string problem() const override {
    if (content.source.empty()) return {};
    const std::vector<std::string>& all = w_->help_scopes();
    if (std::find(all.begin(), all.end(), content.source) != all.end()) return {};
    std::string known;
    for (const std::string& s : all) known += (known.empty() ? "" : " | ") + s;
    return "'" + content.source + "' is not one of this app's key scopes (" + known + ")";
  }
  std::string text() const override { return w_->help_text(content.source); }
};

}  // namespace

// ---- Windows -------------------------------------------------------------------------

Windows::Windows() = default;
Windows::~Windows() = default;

void Windows::bind_document(std::string name, const Document* doc) { documents_[std::move(name)] = doc; }

void Windows::bind_sample_document(std::string name, std::string markdown) {
  DocEntry e;
  e.id = "sample";
  e.text = std::move(markdown);
  Document& d = owned_documents_[name];
  d.entries.assign(1, std::move(e));
  documents_[std::move(name)] = &d;
}
void Windows::bind_rows(std::string name, RowsFn rows) { rows_[std::move(name)] = std::move(rows); }
void Windows::bind_submit(std::string name, SubmitFn submit, OnSubmit on_submit) {
  on_submit_[name] = on_submit;
  submits_[std::move(name)] = std::move(submit);
}
void Windows::bind_note(std::string name, NoteFn note) { notes_[std::move(name)] = std::move(note); }

void Windows::set_highlighter(markdown::Highlighter h) {
  highlighter_ = std::move(h);
  ++highlighter_epoch_;
}

void Windows::set_code_fold(int fold_over_lines, int cap_lines) {
  code_fold_over_lines_ = fold_over_lines;
  code_cap_lines_ = cap_lines;
}

// ONE call registers both halves — the name with the layout vocabulary and the factory
// here — so the vocabulary can never name a kind nothing can build. Rung 1 refuses a
// library name inside register_widget_kind(), which is why the factory is only stored
// after it says yes.
bool Windows::register_kind(std::string name, Factory factory, SourceRule rule, std::string source_is, std::string* why) {
  if (!factory) {
    if (why) *why = "a widget kind needs a factory";
    return false;
  }
  if (!register_widget_kind(name, rule, std::move(source_is), why)) return false;
  factories_[std::move(name)] = std::move(factory);
  return true;
}

Widget* Windows::registered(std::string_view kind, std::string_view source) {
  Content c;
  c.kind = WidgetKind::Registered;
  c.registered_name = std::string(kind);
  c.source = std::string(source);
  if (factories_.find(c.registered_name) == factories_.end()) return nullptr;
  return widget_for(content_to_string(c));
}
void Windows::add_menu(std::string name, std::string json_text) { host_menus_[std::move(name)] = std::move(json_text); }
void Windows::set_dir(std::string dir) { dir_ = std::move(dir); }

void Windows::set_help(std::string lead, std::vector<std::string> scopes, std::string note) {
  help_lead_ = std::move(lead);
  help_scopes_ = std::move(scopes);
  help_note_ = std::move(note);
}

// One scope, or every one the host set. The lead and the note belong to the whole list,
// so a single-scope window shows neither — it is a column of keys, not a help page.
std::string Windows::help_text(std::string_view scope) const {
  if (scope.empty()) return help_document(bindings(), help_lead_, help_scopes_, help_note_);
  return help_document(bindings(), "", {std::string(scope)}, "");
}

void Windows::set_env(WidgetEnv env) { env_ = env; }
const Bindings& Windows::bindings() const { return env_.bindings ? *env_.bindings : default_bindings(); }

Widget* Windows::widget_for(const std::string& content) {
  auto it = by_content_.find(content);
  if (it != by_content_.end()) return it->second.get();
  std::string why;
  std::optional<Content> c = parse_content(content, &why);
  std::unique_ptr<Widget> w;
  if (!c) {
    w = std::make_unique<ErrorWidget>(*this, why);
  } else {
    switch (c->kind) {
      case WidgetKind::Transcript: w = std::make_unique<TranscriptWidget>(*this); break;
      case WidgetKind::Input: w = std::make_unique<InputWidget>(*this); break;
      case WidgetKind::Menu: w = std::make_unique<MenuWidget>(*this); break;
      case WidgetKind::Rows: w = std::make_unique<RowsWidget>(*this); break;
      case WidgetKind::Text: w = std::make_unique<TextWidget>(*this); break;
      case WidgetKind::File: w = std::make_unique<FileWidget>(*this); break;
      case WidgetKind::Help: w = std::make_unique<HelpWidget>(*this); break;
      case WidgetKind::Registered: {
        // Rung 2. The name resolved in the layout vocabulary, so a factory for it exists
        // unless a host cleared the registry behind this Windows' back — which is a named
        // error panel like any other, never a null widget or a blank window.
        auto it = factories_.find(c->registered_name);
        if (it == factories_.end())
          w = std::make_unique<ErrorWidget>(*this, "kind '" + c->registered_name + "' is registered but this host has no factory for it");
        else
          w = it->second();
        if (!w) w = std::make_unique<ErrorWidget>(*this, "kind '" + c->registered_name + "' built nothing");
        break;
      }
    }
    w->content = *c;
  }
  Widget* raw = w.get();
  by_content_[content] = std::move(w);
  return raw;
}

namespace {

void each_window(const Node& n, const std::function<void(const Node&)>& fn) {
  if (n.is_window()) {
    fn(n);
    return;
  }
  for (const Node& c : n.children) each_window(c, fn);
}

}  // namespace

WindowsReport Windows::sync(const WindowStack& stack) {
  WindowsReport rep;
  // m5b: the map is REBUILT IN PLACE, not cleared. `clear()` destroys every node and the
  // next frame allocates them again — three a frame, for a window set that almost never
  // changes. `seen` marks what this pass found; anything unmarked afterwards is gone.
  for (auto& [id, w] : by_window_) w = nullptr;
  for (const Layer& l : stack.layers())
    each_window(l.root, [&](const Node& n) {
      Widget* w = widget_for(n.content);
      by_window_[n.id] = w;  // insert_or_assign: an existing node is reused
      // Phase 13 m5: the "window 'x' (content 'y'): " prefix is built only when there is
      // something to say. It used to be built for every window of every frame and thrown
      // away — a heap allocation per window per paint to describe a problem that almost
      // never exists.
      std::string p = w->problem();
      const std::vector<std::string> notes = p.empty() ? w->notes() : std::vector<std::string>{};
      if (p.empty() && notes.empty()) return;
      const std::string where = "window '" + n.id + "' (content '" + n.content + "'): ";
      if (!p.empty()) rep.bad_values.push_back(where + p);
      for (const std::string& note : notes) rep.bad_values.push_back(where + note);
    });
  for (auto it = by_window_.begin(); it != by_window_.end();)
    it = it->second ? std::next(it) : by_window_.erase(it);
  return rep;
}

void Windows::autosize(WindowStack& stack, Rect box) {
  static thread_local Scratch<std::vector<ResolvedNode>> scratch("autosize nodes");
  auto nodes = scratch.lock();
  stack.resolve_into(box, *nodes);
  for (const ResolvedNode& rn : *nodes) {
    if (!rn.node->is_window()) continue;
    Widget* w = at(rn.node->id);
    if (!w) continue;
    // The parent split is the INNERMOST one that contains this window — the last in
    // tree order, since a container precedes its children and siblings never overlap.
    // It decides the axis (a Row divides width, a Column height) and the extent the
    // widget sizes itself against; with no split above it, that is the layer's box.
    const ResolvedNode* parent = nullptr;
    for (const ResolvedNode& p : *nodes)
      if (!p.node->is_window() && p.layer == rn.layer && p.inner.contains(rn.outer.x, rn.outer.y)) parent = &p;
    const bool row = parent && parent->node->kind == Node::Kind::Row;
    const int extent = !parent ? box.h : row ? parent->inner.w : parent->inner.h;
    const int border = rn.node->border != Border::None ? 2 : 0;
    if (std::optional<int> want = w->desired_outer(rn.inner.w, extent, border))
      if (Node* nd = stack.find(rn.node->id)) nd->size = SplitSize::fixed(Dim::abs(*want));
  }
}

void Windows::layout(const WindowStack& stack, Rect box) {
  static thread_local Scratch<std::vector<ResolvedNode>> scratch("layout nodes");
  auto nodes = scratch.lock();
  stack.resolve_into(box, *nodes);
  for (const ResolvedNode& rn : *nodes)
    if (rn.node->is_window())
      if (Widget* w = at(rn.node->id)) w->layout(rn);
}

WindowsReport Windows::prepare(WindowStack& stack, Rect box) {
  WindowsReport rep = sync(stack);
  autosize(stack, box);
  layout(stack, box);
  return rep;
}

void Windows::draw(const ResolvedNode& rn, Frame& f, const Theme& theme) {
  if (!rn.node->is_window()) return;
  Widget* w = at(rn.node->id);
  if (!w) return;
  if (const std::string why = w->problem(); !why.empty()) {
    ErrorWidget(*this, why).draw(rn, f, theme);
    return;
  }
  w->draw(rn, f, theme);
  draw_scrollbar(rn, *w, f, theme);
}

// The bar lives in the window's RIGHT BORDER COLUMN, which is why the window draws it
// and not the widget: a widget is handed a content rect and knows nothing about whether
// it has a border. A window WITHOUT a border has no track and gets no bar — the `▼ N
// more` marker is the signal there (Phase 12 m5: both are kept, and they answer
// different questions — the marker is the non-graphical one).
void Windows::draw_scrollbar(const ResolvedNode& rn, Widget& w, Frame& f, const Theme& theme) {
  tracks_.erase(rn.node->id);
  if (rn.node->border == Border::None) return;
  const std::optional<Widget::ScrollExtent> e = w.scroll_extent(Widget::Axis::Vertical);
  if (!e) return;
  const int track = rn.outer.h - 2;  // between the corners
  const int x = rn.outer.x + rn.outer.w - 1;
  if (track <= 0 || rn.outer.w < 2) return;
  ScrollThumb t;
  if (!scroll_thumb(*e, track, t)) return;
  tracks_[rn.node->id] = Track{x, rn.outer.y + 1, track};
  Style s = theme.style(Role::scrollbar);
  const Style ground = theme.style(rn.node->background);
  if (s.bg.kind == Color::Kind::None) s.bg = ground.bg;
  // █ (U+2588) is East Asian AMBIGUOUS, exactly like the box-drawing set the border is
  // made of — so it follows the same rule the border already has (Layout.hpp): with
  // `ambiguous_wide` the thumb is ASCII. Drawing the block anyway put a glyph a
  // wide-ambiguous terminal renders in TWO cells into a one-cell border column, which
  // shifts the whole row. Found by Phase 12 m7, and only findable once the thumb stopped
  // being overwritten by the neighbour's border.
  const char* thumb = env_.ambiguous_wide ? "#" : "\xE2\x96\x88";
  for (int i = 0; i < t.length; ++i) {
    const int y = rn.outer.y + 1 + t.offset + i;
    if (y >= rn.outer.y + rn.outer.h - 1) break;
    f.put(x, y, thumb, 1, s);
  }
}

bool Windows::handle(std::string_view window, const Event& e) {
  Widget* w = at(window);
  if (!w || !w->problem().empty()) return false;
  if (handle_scrollbar(window, *w, e)) return true;
  return w->handle(e);
}

// A press in the track column drives the widget — but ONLY a widget that accepted
// scroll_to(). One that merely reports (a menu, whose scroll is derived from its
// selection) gets an accurate bar that is not a handle, which is the whole reason
// Widget's scroll capability is two optional halves.
bool Windows::handle_scrollbar(std::string_view window, Widget& w, const Event& e) {
  const MouseEvent* m = std::get_if<MouseEvent>(&e);
  if (!m) return false;
  const std::string id(window);
  if (m->kind == MouseEvent::Kind::Release) {
    if (bar_drag_ != id) return false;
    bar_drag_.clear();
    return true;
  }
  auto it = tracks_.find(id);
  if (it == tracks_.end()) return false;
  const Track& tr = it->second;
  const std::optional<Widget::ScrollExtent> e2 = w.scroll_extent(Widget::Axis::Vertical);
  if (!e2) return false;
  ScrollThumb th;
  if (!scroll_thumb(*e2, tr.h, th)) return false;
  if (m->kind == MouseEvent::Kind::Press) {
    if (m->button != 1 || m->x != tr.x || m->y < tr.y || m->y >= tr.y + tr.h) return false;
    const int cell = m->y - tr.y;
    // On the thumb: grab it where it was taken, so it does not jump under the pointer.
    // In the trough: jump so the thumb's START lands there, which is the one rule that
    // makes a click and the drag that may follow it agree.
    bar_grab_ = (cell >= th.offset && cell < th.offset + th.length) ? cell - th.offset : 0;
    if (!w.scroll_to(Widget::Axis::Vertical, scroll_first_for_cell(*e2, tr.h, cell - bar_grab_))) return false;
    bar_drag_ = id;
    return true;
  }
  if (m->kind == MouseEvent::Kind::Drag) {
    if (bar_drag_ != id) return false;
    // The pointer may be anywhere by now (the press captured it), so only its ROW counts.
    w.scroll_to(Widget::Axis::Vertical, scroll_first_for_cell(*e2, tr.h, m->y - tr.y - bar_grab_));
    return true;
  }
  return false;
}

InputAction Windows::input_event(std::string_view source, const Event& e) {
  return static_cast<InputWidget*>(widget_for("input:" + std::string(source)))->event(e);
}

Transcript& Windows::transcript(std::string_view source) {
  return static_cast<TranscriptWidget*>(widget_for("transcript:" + std::string(source)))->t;
}

Input& Windows::input(std::string_view source) {
  return static_cast<InputWidget*>(widget_for("input:" + std::string(source)))->ed;
}

void Windows::set_input_min_outer(std::string_view source, int rows) {
  static_cast<InputWidget*>(widget_for("input:" + std::string(source)))->min_outer = rows;
}

Menu& Windows::menu(std::string_view source) {
  return static_cast<MenuWidget*>(widget_for("menu:" + std::string(source)))->menu();
}

std::string Windows::menu_origin(std::string_view source) {
  return static_cast<MenuWidget*>(widget_for("menu:" + std::string(source)))->origin();
}

std::vector<std::string> Windows::menu_names() const {
  std::vector<std::string> out;
  auto add = [&out](std::string name) {
    if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
  };
  if (!dir_.empty()) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir_ + "/menus", ec))
      if (e.path().extension() == ".json") add(e.path().stem().string());
  }
  for (const auto& [name, _] : host_menus_) add(name);
  for (std::string_view name : shipped_menu_names()) add(std::string(name));
  std::sort(out.begin(), out.end());
  return out;
}

Widget* Windows::at(std::string_view window) const {
  auto it = by_window_.find(window);
  return it == by_window_.end() ? nullptr : it->second;
}

std::optional<Content> Windows::content_at(std::string_view window) const {
  Widget* w = at(window);
  if (!w) return std::nullopt;
  return w->content;
}

Transcript* Windows::transcript_at(std::string_view window) const {
  Widget* w = at(window);
  if (!w || w->content.kind != WidgetKind::Transcript) return nullptr;
  return &static_cast<TranscriptWidget*>(w)->t;
}

Input* Windows::input_at(std::string_view window) const {
  Widget* w = at(window);
  if (!w || w->content.kind != WidgetKind::Input) return nullptr;
  return &static_cast<InputWidget*>(w)->ed;
}

Menu* Windows::menu_at(std::string_view window) const {
  Widget* w = at(window);
  if (!w || w->content.kind != WidgetKind::Menu) return nullptr;
  return &static_cast<MenuWidget*>(w)->menu();
}

}  // namespace rolltui
