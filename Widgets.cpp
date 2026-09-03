// rolltui/Widgets.cpp — see Widgets.hpp. Every concrete widget lives here: a host
// reaches them through Windows, so the set of kinds is one table (Layout.hpp) and one
// factory (widget_for), never a chain of names in an application.
#include "rolltui/Widgets.hpp"

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
  if (below > 0) {
    const std::string marker = "\xE2\x96\xBC " + std::to_string(below) + " more ";
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
  const Windows::TextFn* note_fn(const std::string& name) const {
    auto it = w_->notes_.find(name);
    return it == w_->notes_.end() ? nullptr : &it->second;
  }
  const std::string* host_menu(const std::string& name) const {
    auto it = w_->host_menus_.find(name);
    return it == w_->host_menus_.end() ? nullptr : &it->second;
  }
  const Windows::DrawFn* custom_fn(const std::string& name) const {
    auto it = w_->customs_.find(name);
    return it == w_->customs_.end() ? nullptr : &it->second;
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

  std::string problem() const override { return document(content.source) ? std::string() : unbound(); }
  void layout(const ResolvedNode& rn) override {
    if (const Document* d = document(content.source)) t.layout(*d, rn.inner, options(rn));
  }
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    const Document* d = document(content.source);
    if (!d) return;
    t.layout(*d, rn.inner, options(rn));
    t.draw(f, theme);
  }
  bool handle(const Event& e) override {
    const Document* d = document(content.source);
    return d && t.handle(e, *d, env().now_ms, binds());
  }

 private:
  TranscriptOptions options(const ResolvedNode& rn) const {
    TranscriptOptions o;
    o.ambiguous_wide = amb();
    o.inset = rn.node->border != Border::None ? 1 : 0;
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
    const std::string note = note_text();
    if (note.empty()) return;
    const int nw = unicode::display_width(note, amb());
    if (tr.h < r.h) {  // its own row, under the text
      f.put_text(r.x, r.y + tr.h, note, theme.style(Role::text_muted), std::max(r.w, 0), amb());
      return;
    }
    const int nx = std::max(r.x + r.w - nw, r.x + end_col() + 2);
    f.put_text(nx, r.y, note, theme.style(Role::text_muted), std::max(r.x + r.w - nx, 0), amb());
  }
  // A layout-declared input with no host code still edits and submits; a host that
  // wants the action back (to quit on Eof, to offer an Ignored key elsewhere) calls
  // event() through Windows::input_event instead.
  InputAction event(const Event& e) {
    const InputAction a = ed.handle(e, binds(), env().now_ms);
    if (a != InputAction::Submit) return a;
    std::string text = ed.text();
    ed.push_history(text);
    ed.clear();
    if (const Windows::SubmitFn* fn = submit_fn(content.source); fn && *fn) (*fn)(text);
    return a;
  }
  bool handle(const Event& e) override { return event(e) != InputAction::Ignored; }

 private:
  std::string note_text() const {
    const Windows::TextFn* fn = note_fn(content.source);
    return fn && *fn ? (*fn)() : std::string();
  }
  // The column just past the text (or past the placeholder while it is empty): where a
  // note may sit on the first row.
  int end_col() const {
    if (!ed.text().empty()) return ed.cell_of(ed.text().size()).col;
    return unicode::display_width(ed.options().prompt, amb()) + unicode::display_width(ed.options().placeholder, amb());
  }
  // Rows of window text: the text's rows, capped at half the parent, plus one for the
  // note when it does not fit beside a single row.
  int rows_with_note(int width, int text_rows) const {
    const std::string note = note_text();
    return input_rows(text_rows, end_col(), note.empty() ? 0 : unicode::display_width(note, amb()), width, max_rows_);
  }
  // The note takes a row of its own exactly when the window has more than one: with a
  // single row it sits beside the text (and rows_with_note only ever returns 1 with a
  // note when it fits there, or when half the parent leaves no room for a second row).
  bool note_owns_row(int width) const {
    return !note_text().empty() && rows_with_note(width, ed.rows_for(width)) > 1;
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
  void layout(const ResolvedNode&) override {}
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    const Windows::RowsFn* fn = rows_fn(content.source);
    if (!fn || !*fn) return;
    const std::vector<Row> rows = (*fn)();
    const Rect r = rn.inner;
    const Style label = theme.style(Role::label), value = theme.style(Role::value);
    if (r.h == 1) {
      std::string s;
      for (const Row& row : rows) s += (s.empty() ? "" : "  ") + row.label + " " + row.value;
      f.put_text(r.x + 1, r.y, s, value, std::max(r.w - 1, 0), amb());
      return;
    }
    WrapOptions wo;
    wo.ambiguous_wide = amb();
    int y = r.y;
    for (const Row& row : rows) {
      if (y >= r.y + r.h) break;
      f.put_text(r.x + 1, y, row.label, label, std::max(r.w - 1, 0), amb());
      std::vector<Line> lines = wrap(row.value, std::max(r.w - 9, 1), wo);
      if (lines.empty()) lines.push_back({});
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
// rebinding. Which scopes, and any lead/note lines, are the host's (set_help).
class HelpWidget : public ScrollTextWidget {
 public:
  using ScrollTextWidget::ScrollTextWidget;
  std::string text() const override { return w_->help_text(); }
};

// custom:<name> — the host's own composite, bound by name.
class CustomWidget : public WidgetBase {
 public:
  using WidgetBase::WidgetBase;
  std::string problem() const override { return custom_fn(content.source) ? std::string() : unbound(); }
  void layout(const ResolvedNode&) override {}
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    if (const Windows::DrawFn* fn = custom_fn(content.source); fn && *fn) (*fn)(rn, f, theme);
  }
};

}  // namespace

// ---- Windows -------------------------------------------------------------------------

Windows::Windows() = default;
Windows::~Windows() = default;

void Windows::bind_document(std::string name, const Document* doc) { documents_[std::move(name)] = doc; }
void Windows::bind_rows(std::string name, RowsFn rows) { rows_[std::move(name)] = std::move(rows); }
void Windows::bind_submit(std::string name, SubmitFn submit) { submits_[std::move(name)] = std::move(submit); }
void Windows::bind_note(std::string name, TextFn note) { notes_[std::move(name)] = std::move(note); }
void Windows::bind_custom(std::string name, DrawFn draw) { customs_[std::move(name)] = std::move(draw); }
void Windows::add_menu(std::string name, std::string json_text) { host_menus_[std::move(name)] = std::move(json_text); }
void Windows::set_dir(std::string dir) { dir_ = std::move(dir); }

void Windows::set_help(std::string lead, std::vector<std::string> scopes, std::string note) {
  help_lead_ = std::move(lead);
  help_scopes_ = std::move(scopes);
  help_note_ = std::move(note);
}

std::string Windows::help_text() const { return help_document(bindings(), help_lead_, help_scopes_, help_note_); }

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
      case WidgetKind::Custom: w = std::make_unique<CustomWidget>(*this); break;
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
  by_window_.clear();
  for (const Layer& l : stack.layers())
    each_window(l.root, [&](const Node& n) {
      Widget* w = widget_for(n.content);
      by_window_[n.id] = w;
      const std::string where = "window '" + n.id + "' (content '" + n.content + "'): ";
      if (std::string p = w->problem(); !p.empty()) rep.bad_values.push_back(where + p);
      for (const std::string& note : w->notes()) rep.bad_values.push_back(where + note);
    });
  return rep;
}

void Windows::autosize(WindowStack& stack, Rect box) {
  const std::vector<ResolvedNode> nodes = stack.resolve(box);
  for (const ResolvedNode& rn : nodes) {
    if (!rn.node->is_window()) continue;
    Widget* w = at(rn.node->id);
    if (!w) continue;
    // The parent split is the INNERMOST one that contains this window — the last in
    // tree order, since a container precedes its children and siblings never overlap.
    // It decides the axis (a Row divides width, a Column height) and the extent the
    // widget sizes itself against; with no split above it, that is the layer's box.
    const ResolvedNode* parent = nullptr;
    for (const ResolvedNode& p : nodes)
      if (!p.node->is_window() && p.layer == rn.layer && p.inner.contains(rn.outer.x, rn.outer.y)) parent = &p;
    const bool row = parent && parent->node->kind == Node::Kind::Row;
    const int extent = !parent ? box.h : row ? parent->inner.w : parent->inner.h;
    const int border = rn.node->border != Border::None ? 2 : 0;
    if (std::optional<int> want = w->desired_outer(rn.inner.w, extent, border))
      if (Node* nd = stack.find(rn.node->id)) nd->size = SplitSize::fixed(Dim::abs(*want));
  }
}

void Windows::layout(const WindowStack& stack, Rect box) {
  for (const ResolvedNode& rn : stack.resolve(box))
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
}

bool Windows::handle(std::string_view window, const Event& e) {
  Widget* w = at(window);
  return w && w->problem().empty() && w->handle(e);
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

std::string Windows::custom_at(std::string_view window) const {
  Widget* w = at(window);
  if (!w || w->content.kind != WidgetKind::Custom) return {};
  return w->content.source;
}

}  // namespace rolltui
