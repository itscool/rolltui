// rolltui/InputCpp.cpp — the C++ side of the input widget, behind the same boundary as
// `c/rolltui_input.c` (rolltui/c/rolltui_input.h, Phase 15 m5). One CMake flag picks which of
// the two links; both satisfy `rolltui/tests/input_test.cpp`, the menu's typed fields, every
// golden frame and roll's own prompt, so a behavioural difference is a test failure on the
// day it appears rather than a review comment.
//
// This is the code Phase 9 m10 wrote and Phase 12 m1 grew undo into, moved behind the
// boundary. It is deliberately NOT a transliteration of the C: it keeps `std::string`,
// `std::vector`, `std::optional<int>` for the goal column and `UndoStack<T>` — the comparison
// m5 is here to make is between two languages writing the same design naturally.
#include "rolltui/c/rolltui_input.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Undo.hpp"
#include "rolltui/c/rolltui_unicode.h"

using rolltui::UndoStack;

namespace {

std::string_view view(const RolltuiStr& s) { return std::string_view(s.p ? s.p : "", s.n); }

// How long an open "ordinary editing" group stays open with no further edit before the next
// one is treated as a fresh group instead of a continuation (Input.hpp's UNDO).
constexpr std::uint64_t kUndoGroupTimeoutMs = 700;

// What may enter the text: CR LF and CR become LF; '\n' and '\t' pass; every other control
// character (and DEL) is dropped.
std::string sanitise(std::string_view in, bool drop_newlines) {
  std::string out;
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c == '\r') {
      if (!drop_newlines) out.push_back('\n');
      if (i + 1 < in.size() && in[i + 1] == '\n') ++i;
      continue;
    }
    if (c == '\n') {
      if (!drop_newlines) out.push_back('\n');
      continue;
    }
    if (c == '\t' || (c >= 0x20 && c != 0x7F)) out.push_back(static_cast<char>(c));
  }
  return out;
}

struct Snapshot {
  std::string text;
  std::size_t caret = 0;
  RolltuiInputSelection sel;
  bool operator==(const Snapshot&) const = default;
};

// One grapheme's place in the wrap.
struct FlowCell {
  int row = 0, col = 0, width = 0;
};

struct Flow {
  std::vector<FlowCell> cells;        // one per grapheme
  std::vector<std::size_t> row_end;   // per row: the position at its end
  int end_row = 0, end_col = 0;
  int rows = 1;
};

}  // namespace

struct RolltuiInput {
  std::string text;
  std::vector<RolltuiUnicodeGrapheme> g;
  std::size_t caret = 0;
  RolltuiInputSelection sel;
  std::optional<int> goal_col;
  RolltuiInputOptions opt;
  int prompt_w = 2;

  UndoStack<Snapshot> undo{Snapshot{}};
  bool undo_pending = false;
  std::uint64_t undo_last_ms = 0, now_ms = 0;

  std::vector<std::string> hist;
  std::size_t hist_pos = 0;
  std::string draft;

  RolltuiRect area;
  int width = 80, top = 0;
  mutable bool dirty = true;
  mutable Flow flow;

  struct Drag {
    bool active = false;
    std::size_t begin = 0, end = 0;
    int x = 0, y = 0;
  } drag;
  struct Click {
    std::uint64_t at_ms = 0;
    int x = -1, y = -1, count = 0;
  } click;

  RolltuiCopyFn copy_fn = nullptr;
  void* copy_ctx = nullptr;
  RolltuiUnicodeScratch* u = nullptr;  // OWNED: the grapheme walk's working memory

  // ---- the mechanics, as members so they read the way the class always did ----
  Snapshot snapshot() const { return {text, caret, sel}; }
  bool sel_empty() const { return !sel.active || sel.anchor == sel.head; }
  std::size_t sel_begin() const { return sel.begin(); }
  std::size_t sel_end() const { return sel.end(); }

  void retext(std::string t, std::size_t c) {
    text = std::move(t);
    g.resize(text.size() + 1);
    g.resize(rolltui_u_graphemes(u, text.data(), text.size(), opt.ambiguous_wide, g.data()));
    caret = snap(c);
    if (sel.active) {
      sel.anchor = snap(sel.anchor);
      sel.head = snap(sel.head);
    }
    dirty = true;
  }

  std::size_t snap(std::size_t pos) const {
    pos = std::min(pos, text.size());
    auto it = std::lower_bound(g.begin(), g.end(), pos,
                               [](const RolltuiUnicodeGrapheme& x, std::size_t p) { return x.offset < p; });
    return it == g.end() ? text.size() : it->offset;
  }
  std::size_t prev_boundary(std::size_t pos) const {
    auto it = std::lower_bound(g.begin(), g.end(), pos,
                               [](const RolltuiUnicodeGrapheme& x, std::size_t p) { return x.offset < p; });
    return it == g.begin() ? 0 : (it - 1)->offset;
  }
  std::size_t next_boundary(std::size_t pos) const {
    auto it = std::upper_bound(g.begin(), g.end(), pos,
                               [](std::size_t p, const RolltuiUnicodeGrapheme& x) { return p < x.offset; });
    return it == g.end() ? text.size() : it->offset;
  }
  std::size_t line_start(std::size_t pos) const {
    if (pos == 0) return 0;
    const std::size_t nl = text.rfind('\n', pos - 1);
    return nl == std::string::npos ? 0 : nl + 1;
  }
  std::size_t line_end(std::size_t pos) const {
    const std::size_t nl = text.find('\n', pos);
    return nl == std::string::npos ? text.size() : nl;
  }

  void close_group() {
    if (!undo_pending) return;
    undo.commit(snapshot());
    undo_pending = false;
  }

  // Called after every mutating primitive with the snapshot taken just before it ran.
  void note_edit(bool atomic, const Snapshot& pre) {
    const Snapshot post = snapshot();
    if (post == pre) return;  // nothing actually changed
    const std::uint64_t elapsed =
        now_ms >= undo_last_ms ? now_ms - undo_last_ms : kUndoGroupTimeoutMs + 1;
    const bool continues = !atomic && undo_pending && elapsed <= kUndoGroupTimeoutMs;
    if (!continues) {
      if (undo_pending) {
        undo.commit(pre);  // an open group closes as a REAL step
      } else if (!(undo.current() == pre)) {
        // No group was open, yet the live state drifted from the last checkpoint — one or
        // more caret/selection moves happened since. A move is never its own undo step, so
        // this folds the drift into the existing checkpoint rather than committing.
        undo.replace_current(pre);
      }
    }
    if (atomic) {
      undo.commit(post);
      undo_pending = false;
    } else {
      undo_pending = true;
      undo_last_ms = now_ms;
    }
  }

  void place(std::size_t pos, bool extend) {
    close_group();  // a caret/selection move with no text change closes any open group
    pos = snap(pos);
    if (extend) {
      if (!sel.active) sel.anchor = caret;
      sel.head = pos;
      sel.active = 1;
    } else {
      sel = {};
    }
    caret = pos;
  }

  void set_caret(std::size_t byte, bool extend) {
    place(byte, extend);
    goal_col.reset();
  }

  void erase_range(std::size_t b, std::size_t e) {
    b = std::min(b, text.size());
    e = std::min(e, text.size());
    if (b >= e) return;
    std::string t = text;
    t.erase(b, e - b);
    std::size_t c = caret;
    if (c >= e) c -= (e - b);
    else if (c > b) c = b;
    sel = {};
    retext(std::move(t), c);
    goal_col.reset();
  }

  bool erase_selection() {
    if (sel_empty()) {
      sel = {};
      return false;
    }
    erase_range(sel_begin(), sel_end());
    return true;
  }

  void raw_insert(std::string_view utf8) {
    std::string s = sanitise(utf8, opt.single_line != 0);
    erase_selection();
    if (s.empty()) return;
    std::string t = text;
    t.insert(caret, s);
    sel = {};
    retext(std::move(t), caret + s.size());
    goal_col.reset();
  }

  void apply_snapshot(const Snapshot& s) {
    goal_col.reset();
    sel = s.sel;
    retext(s.text, s.caret);
  }

  Flow make_flow(int width) const {
    Flow f;
    f.cells.assign(g.size(), {});
    const int indent = prompt_w;
    const int cap = std::max(width - 2 * opt.inset, indent + 1);
    const int tab = std::max(opt.tab_width, 1);
    int row = 0, col = indent;
    for (std::size_t i = 0; i < g.size(); ++i) {
      const char c0 = text[g[i].offset];
      if (c0 == '\n') {
        f.cells[i] = {row, col, 0};
        f.row_end.push_back(g[i].offset);
        ++row;
        col = indent;
        continue;
      }
      int w = c0 == '\t' ? tab - ((col - indent) % tab) : g[i].width;
      if (w > 0 && col + w > cap && col > indent) {
        f.row_end.push_back(g[i].offset);
        ++row;
        col = indent;
        if (c0 == '\t') w = tab;
      }
      f.cells[i] = {row, col, w};
      col += w;
    }
    if (col >= cap && col > indent) {  // a full row: the end of the text starts the next
      f.row_end.push_back(text.size());
      ++row;
      col = indent;
    }
    f.row_end.push_back(text.size());
    f.end_row = row;
    f.end_col = col;
    f.rows = row + 1;
    return f;
  }

  void ensure() const {
    if (!dirty) return;
    flow = make_flow(width);
    dirty = false;
  }

  void cell_of(std::size_t offset, int& row, int& col) const {
    ensure();
    offset = snap(offset);
    if (offset >= text.size()) {
      row = flow.end_row;
      col = flow.end_col;
      return;
    }
    auto it = std::lower_bound(g.begin(), g.end(), offset,
                               [](const RolltuiUnicodeGrapheme& x, std::size_t p) { return x.offset < p; });
    const std::size_t i = static_cast<std::size_t>(it - g.begin());
    row = flow.cells[i].row;
    col = flow.cells[i].col;
  }

  // The position on `row` at column `col`: the grapheme covering it, the first grapheme
  // right of it (a column over the prompt / indent), else the row's end.
  std::size_t pos_at(int row, int col) const {
    ensure();
    row = std::clamp(row, 0, flow.rows - 1);
    for (std::size_t i = 0; i < g.size(); ++i) {
      const FlowCell& c = flow.cells[i];
      if (c.row != row) continue;
      if (col < c.col) return g[i].offset;
      if (col < c.col + std::max(c.width, 1)) return g[i].offset;
    }
    return flow.row_end[static_cast<std::size_t>(row)];
  }

  void unit_around(std::size_t off, bool word, std::size_t& b, std::size_t& e) const {
    off = std::min(off, text.size());
    if (word) {
      rolltui_u_word_range(u, text.data(), text.size(), off, &b, &e);
      return;
    }
    b = line_start(off);
    e = line_end(off);
  }

  void fire_copy() {
    if (!copy_fn) return;
    if (sel_empty()) {
      copy_fn(copy_ctx, "", 0);
      return;
    }
    copy_fn(copy_ctx, text.data() + sel_begin(), sel_end() - sel_begin());
  }

  void drag_to(int x, int y) {
    close_group();  // a drag only moves the selection
    drag.x = x;
    drag.y = y;
    std::size_t b = 0, e = 0;
    if (!hit(x, y, b, e)) return;
    if (click.count >= 2) unit_around(b, click.count == 2, b, e);
    if (b < drag.begin) {
      sel.anchor = drag.end;
      sel.head = b;
    } else {
      sel.anchor = drag.begin;
      sel.head = std::max(e, drag.end);
    }
    sel.active = 1;
    caret = sel.head;
  }

  bool hit(int x, int y, std::size_t& hb, std::size_t& he) const {
    if (area.w <= 0 && area.h <= 0) return false;
    ensure();
    const int row = std::clamp(top + (y - area.y), 0, flow.rows - 1);
    const int col = x - area.x;
    for (std::size_t i = 0; i < g.size(); ++i) {
      const FlowCell& c = flow.cells[i];
      if (c.row != row) continue;
      const bool newline = text[g[i].offset] == '\n';
      if (c.width == 0 && !newline) continue;  // draws nothing, so never "under" a pointer
      if (col < c.col || (!newline && col < c.col + std::max(c.width, 1))) {
        hb = g[i].offset;
        he = (newline || col < c.col) ? g[i].offset : g[i].offset + g[i].length;
        return true;
      }
      if (newline) break;
    }
    hb = he = flow.row_end[static_cast<std::size_t>(row)];
    return true;
  }
};

// ---- options ---------------------------------------------------------------------------------

extern "C" void rolltui_input_options_init(RolltuiInputOptions* o) {
  o->ambiguous_wide = 0;
  o->tab_width = 4;
  o->inset = 0;
  o->prompt = std::string_view("> ");
  o->prompt_role = static_cast<rolltui::Role>(ROLLTUI_ROLE_DEFAULT_PROMPT);
  o->placeholder.clear();
  o->multi_click_ms = 400;
  o->history_limit = 1000;
  o->single_line = 0;
}

extern "C" void rolltui_input_options_release(RolltuiInputOptions* o) {
  rolltui_str_free(&o->prompt);
  rolltui_str_free(&o->placeholder);
}

extern "C" void rolltui_input_options_copy(RolltuiInputOptions* to, const RolltuiInputOptions* from) {
  if (to != from) *to = *from;
}

extern "C" int rolltui_input_options_equal(const RolltuiInputOptions* a, const RolltuiInputOptions* b) {
  return a->ambiguous_wide == b->ambiguous_wide && a->tab_width == b->tab_width && a->inset == b->inset &&
                 a->prompt_role == b->prompt_role && a->multi_click_ms == b->multi_click_ms &&
                 a->history_limit == b->history_limit && a->single_line == b->single_line &&
                 view(a->prompt) == view(b->prompt) && view(a->placeholder) == view(b->placeholder)
             ? 1
             : 0;
}

// ---- lifetime ---------------------------------------------------------------------------------

extern "C" RolltuiInput* rolltui_input_new(void) {
  std::unique_ptr<RolltuiInput> in = std::make_unique<RolltuiInput>();
  rolltui_input_options_init(&in->opt);
  in->u = rolltui_u_scratch_new();
  return in.release();
}

extern "C" void rolltui_input_free(RolltuiInput* in) {
  const std::unique_ptr<RolltuiInput> owned(in);
  if (in) rolltui_u_scratch_free(in->u);
}

extern "C" void rolltui_input_set_copy(RolltuiInput* in, RolltuiCopyFn fn, void* ctx) {
  in->copy_fn = fn;
  in->copy_ctx = ctx;
}

// ---- content ------------------------------------------------------------------------------------

extern "C" const char* rolltui_input_text(const RolltuiInput* in, std::size_t* len) {
  if (len) *len = in->text.size();
  return in->text.c_str();
}

extern "C" std::size_t rolltui_input_caret(const RolltuiInput* in) { return in->caret; }

extern "C" void rolltui_input_selection(const RolltuiInput* in, RolltuiInputSelection* out) { *out = in->sel; }

extern "C" const char* rolltui_input_selected_text(const RolltuiInput* in, std::size_t* len) {
  if (in->sel_empty()) {
    *len = 0;
    return "";
  }
  *len = in->sel_end() - in->sel_begin();
  return in->text.data() + in->sel_begin();
}

extern "C" void rolltui_input_set_text(RolltuiInput* in, const char* text, std::size_t len) {
  in->sel = {};
  in->goal_col.reset();
  std::string s = sanitise(std::string_view(text ? text : "", len), false);
  const std::size_t n = s.size();
  in->retext(std::move(s), n);
  // A bulk replace is a fresh document, not an edit (Input.hpp's UNDO).
  in->undo.reset(in->snapshot());
  in->undo_pending = false;
}

extern "C" void rolltui_input_clear(RolltuiInput* in) {
  rolltui_input_set_text(in, "", 0);
  in->hist_pos = in->hist.size();
  in->draft.clear();
}

extern "C" void rolltui_input_set_caret(RolltuiInput* in, std::size_t byte, int extend) {
  in->set_caret(byte, extend != 0);
}

extern "C" void rolltui_input_select_all(RolltuiInput* in) {
  if (in->text.empty()) return;
  in->close_group();
  in->sel = {0, in->text.size(), 1};
  in->caret = in->text.size();
  in->goal_col.reset();
}

extern "C" void rolltui_input_clear_selection(RolltuiInput* in) { in->sel = {}; }

// ---- editing --------------------------------------------------------------------------------------

extern "C" void rolltui_input_insert(RolltuiInput* in, const char* utf8, std::size_t len) {
  const Snapshot pre = in->snapshot();
  const bool replace = !in->sel_empty();  // typing over a selection is a selection-replace
  in->raw_insert(std::string_view(utf8 ? utf8 : "", len));
  in->note_edit(replace, pre);
}

extern "C" void rolltui_input_preview_insert(const RolltuiInput* in, const char* utf8, std::size_t len,
                                             RolltuiStr* out) {
  const bool has_sel = !in->sel_empty();
  const std::size_t b = has_sel ? in->sel_begin() : in->caret;
  const std::size_t e = has_sel ? in->sel_end() : in->caret;
  std::string t(in->text, 0, b);
  t += sanitise(std::string_view(utf8 ? utf8 : "", len), in->opt.single_line != 0);
  t.append(in->text, e, in->text.size() - e);
  *out = t;
}

extern "C" int rolltui_input_erase_selection(RolltuiInput* in) { return in->erase_selection() ? 1 : 0; }

extern "C" void rolltui_input_erase_backward(RolltuiInput* in) {
  const Snapshot pre = in->snapshot();
  if (in->erase_selection()) {
    in->note_edit(true, pre);  // a selection-replace
    return;
  }
  if (in->caret == 0) return;
  in->erase_range(in->prev_boundary(in->caret), in->caret);
  in->note_edit(false, pre);
}

extern "C" void rolltui_input_erase_forward(RolltuiInput* in) {
  const Snapshot pre = in->snapshot();
  if (in->erase_selection()) {
    in->note_edit(true, pre);
    return;
  }
  if (in->caret >= in->text.size()) return;
  in->erase_range(in->caret, in->next_boundary(in->caret));
  in->note_edit(false, pre);
}

namespace {
bool is_space_at(const std::string& s, std::size_t pos) {
  return pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n');
}
}  // namespace

extern "C" std::size_t rolltui_input_word_left_of(const RolltuiInput* in, std::size_t pos) {
  std::size_t p = std::min(pos, in->text.size());
  while (p > 0 && is_space_at(in->text, in->prev_boundary(p))) p = in->prev_boundary(p);
  if (p == 0) return 0;
  std::size_t b = 0, e = 0;
  rolltui_u_word_range(in->u, in->text.data(), in->text.size(), in->prev_boundary(p), &b, &e);
  return b;
}

extern "C" std::size_t rolltui_input_word_right_of(const RolltuiInput* in, std::size_t pos) {
  std::size_t p = std::min(pos, in->text.size());
  while (p < in->text.size() && is_space_at(in->text, p)) p = in->next_boundary(p);
  if (p >= in->text.size()) return in->text.size();
  std::size_t b = 0, e = 0;
  rolltui_u_word_range(in->u, in->text.data(), in->text.size(), p, &b, &e);
  return e;
}

extern "C" void rolltui_input_kill_word_backward(RolltuiInput* in) {
  const Snapshot pre = in->snapshot();
  if (!in->erase_selection()) in->erase_range(rolltui_input_word_left_of(in, in->caret), in->caret);
  in->note_edit(true, pre);
}

extern "C" void rolltui_input_kill_word_forward(RolltuiInput* in) {
  const Snapshot pre = in->snapshot();
  if (!in->erase_selection()) in->erase_range(in->caret, rolltui_input_word_right_of(in, in->caret));
  in->note_edit(true, pre);
}

extern "C" void rolltui_input_kill_to_line_start(RolltuiInput* in) {
  const Snapshot pre = in->snapshot();
  in->sel = {};
  in->erase_range(in->line_start(in->caret), in->caret);
  in->note_edit(true, pre);
}

extern "C" void rolltui_input_kill_to_line_end(RolltuiInput* in) {
  const Snapshot pre = in->snapshot();
  in->sel = {};
  in->erase_range(in->caret, in->line_end(in->caret));
  in->note_edit(true, pre);
}

// ---- undo -------------------------------------------------------------------------------------------

extern "C" int rolltui_input_undo(RolltuiInput* in) {
  in->close_group();
  if (!in->undo.undo()) return 0;
  in->apply_snapshot(in->undo.current());
  return 1;
}

extern "C" int rolltui_input_redo(RolltuiInput* in) {
  in->close_group();
  if (!in->undo.redo()) return 0;
  in->apply_snapshot(in->undo.current());
  return 1;
}

extern "C" int rolltui_input_can_undo(const RolltuiInput* in) {
  return in->undo_pending || in->undo.can_undo() ? 1 : 0;
}
extern "C" int rolltui_input_can_redo(const RolltuiInput* in) {
  return !in->undo_pending && in->undo.can_redo() ? 1 : 0;
}

// ---- motions ------------------------------------------------------------------------------------------

extern "C" void rolltui_input_move_left(RolltuiInput* in, int extend) {
  if (!extend && !in->sel_empty()) {
    in->set_caret(in->sel_begin(), false);
    return;
  }
  in->set_caret(in->caret == 0 ? 0 : in->prev_boundary(in->caret), extend != 0);
}

extern "C" void rolltui_input_move_right(RolltuiInput* in, int extend) {
  if (!extend && !in->sel_empty()) {
    in->set_caret(in->sel_end(), false);
    return;
  }
  in->set_caret(in->next_boundary(in->caret), extend != 0);
}

extern "C" void rolltui_input_move_word_left(RolltuiInput* in, int extend) {
  in->set_caret(rolltui_input_word_left_of(in, in->caret), extend != 0);
}
extern "C" void rolltui_input_move_word_right(RolltuiInput* in, int extend) {
  in->set_caret(rolltui_input_word_right_of(in, in->caret), extend != 0);
}
extern "C" void rolltui_input_move_line_start(RolltuiInput* in, int extend) {
  in->set_caret(in->line_start(in->caret), extend != 0);
}
extern "C" void rolltui_input_move_line_end(RolltuiInput* in, int extend) {
  in->set_caret(in->line_end(in->caret), extend != 0);
}

extern "C" int rolltui_input_move_up(RolltuiInput* in, int extend) {
  in->ensure();
  int row = 0, col = 0;
  in->cell_of(in->caret, row, col);
  if (row == 0) return 0;
  const int goal = in->goal_col.value_or(col);
  in->place(in->pos_at(row - 1, goal), extend != 0);
  in->goal_col = goal;
  return 1;
}

extern "C" int rolltui_input_move_down(RolltuiInput* in, int extend) {
  in->ensure();
  int row = 0, col = 0;
  in->cell_of(in->caret, row, col);
  if (row >= in->flow.rows - 1) return 0;
  const int goal = in->goal_col.value_or(col);
  in->place(in->pos_at(row + 1, goal), extend != 0);
  in->goal_col = goal;
  return 1;
}

// ---- history --------------------------------------------------------------------------------------------

extern "C" void rolltui_input_push_history(RolltuiInput* in, const char* entry, std::size_t len) {
  const std::string_view e(entry ? entry : "", len);
  if (!e.empty() && (in->hist.empty() || in->hist.back() != e)) {
    in->hist.emplace_back(e);
    while (in->opt.history_limit > 0 && in->hist.size() > in->opt.history_limit)
      in->hist.erase(in->hist.begin());
  }
  in->hist_pos = in->hist.size();
  in->draft.clear();
}

extern "C" std::size_t rolltui_input_history_count(const RolltuiInput* in) { return in->hist.size(); }

extern "C" const char* rolltui_input_history_at(const RolltuiInput* in, std::size_t i, std::size_t* len) {
  if (i >= in->hist.size()) {
    if (len) *len = 0;
    return "";
  }
  if (len) *len = in->hist[i].size();
  return in->hist[i].c_str();
}

extern "C" std::size_t rolltui_input_history_cursor(const RolltuiInput* in) { return in->hist_pos; }

extern "C" int rolltui_input_history_prev(RolltuiInput* in) {
  if (in->hist_pos == 0 || in->hist.empty()) return 0;
  if (in->hist_pos >= in->hist.size()) {
    in->hist_pos = in->hist.size();
    in->draft = in->text;
  }
  --in->hist_pos;
  const std::string entry = in->hist[in->hist_pos];
  rolltui_input_set_text(in, entry.data(), entry.size());
  return 1;
}

extern "C" int rolltui_input_history_next(RolltuiInput* in) {
  if (in->hist_pos >= in->hist.size()) return 0;
  ++in->hist_pos;
  const std::string entry = in->hist_pos == in->hist.size() ? in->draft : in->hist[in->hist_pos];
  rolltui_input_set_text(in, entry.data(), entry.size());
  return 1;
}

// ---- layout ------------------------------------------------------------------------------------------------

extern "C" void rolltui_input_set_options(RolltuiInput* in, const RolltuiInputOptions* o) {
  const bool retab = o->ambiguous_wide != in->opt.ambiguous_wide;
  in->opt = *o;
  in->prompt_w =
      rolltui_u_display_width(in->u, in->opt.prompt.p, in->opt.prompt.n, in->opt.ambiguous_wide);
  if (retab) {
    in->g.resize(in->text.size() + 1);
    in->g.resize(rolltui_u_graphemes(in->u, in->text.data(), in->text.size(), in->opt.ambiguous_wide,
                                     in->g.data()));
  }
  in->dirty = true;
}

extern "C" const RolltuiInputOptions* rolltui_input_options(const RolltuiInput* in) { return &in->opt; }

extern "C" int rolltui_input_rows_for(const RolltuiInput* in, int width) {
  if (width == in->width) {
    in->ensure();
    return in->flow.rows;
  }
  return in->make_flow(width).rows;
}

extern "C" int rolltui_input_rows(const RolltuiInput* in) {
  in->ensure();
  return in->flow.rows;
}

extern "C" void rolltui_input_layout(RolltuiInput* in, RolltuiRect area) {
  in->area = {area.x + in->opt.inset, area.y, std::max(area.w - 2 * in->opt.inset, 0), area.h};
  if (area.w != in->width) {
    in->width = area.w;
    in->dirty = true;
  }
  in->ensure();
  const int h = std::max(in->area.h, 1);
  int row = 0, col = 0;
  in->cell_of(in->caret, row, col);
  if (row < in->top) in->top = row;
  if (row >= in->top + h) in->top = row - h + 1;
  in->top = std::clamp(in->top, 0, std::max(in->flow.rows - h, 0));
}

extern "C" int rolltui_input_top_row(const RolltuiInput* in) { return in->top; }
extern "C" void rolltui_input_area(const RolltuiInput* in, RolltuiRect* out) { *out = in->area; }

extern "C" void rolltui_input_cell_of(const RolltuiInput* in, std::size_t offset, int* row, int* col) {
  in->cell_of(offset, *row, *col);
}

extern "C" int rolltui_input_hit(const RolltuiInput* in, int x, int y, std::size_t* begin, std::size_t* end) {
  return in->hit(x, y, *begin, *end) ? 1 : 0;
}

// ---- events --------------------------------------------------------------------------------------------------

namespace {

// The command table, in the order `RolltuiInputActions` declares its members.
enum Cmd {
  kSubmit = 0, kNewline, kBackspace, kDelete, kKillWordBack, kKillWordFwd, kKillLineStart, kKillLineEnd,
  kLeft, kRight, kWordLeft, kWordRight, kLineStart, kLineEnd, kUp, kDown, kSelLeft, kSelRight, kSelWordLeft,
  kSelWordRight, kSelLineStart, kSelLineEnd, kSelUp, kSelDown, kSelectAll, kClearSel, kCopy, kEof, kUndo,
  kRedo, kCmdCount
};

int command_of(const RolltuiInputActions* a, std::string_view action) {
  const char* const* names = reinterpret_cast<const char* const*>(a);
  for (int i = 0; i < kCmdCount; ++i)
    if (names[i] && action == names[i]) return i;
  return -1;
}

unsigned char handle_key(RolltuiInput* in, const RolltuiChord& k, const RolltuiBindings* b,
                         const RolltuiInputActions* actions) {
  // Text is text: a printable character without Ctrl or Alt inserts and is never an action.
  if (k.key == ROLLTUI_KEY_CHAR && !k.ctrl && !k.alt) {
    if (k.ch < 0x20 || k.ch == 0x7F) return ROLLTUI_INPUT_IGNORED;
    char buf[4];
    const std::size_t n = rolltui_u_append_utf8(k.ch, buf);
    rolltui_input_insert(in, buf, n);
    return ROLLTUI_INPUT_HANDLED;
  }
  std::size_t alen = 0;
  const char* a = rolltui_bindings_action_for(b, &k, "input", 5, &alen);
  if (!a) return ROLLTUI_INPUT_IGNORED;
  const int cmd = command_of(actions, std::string_view(a, alen));
  if (cmd < 0) return ROLLTUI_INPUT_IGNORED;
  switch (cmd) {
    case kSubmit: return ROLLTUI_INPUT_SUBMIT;
    case kNewline: rolltui_input_insert(in, "\n", 1); return ROLLTUI_INPUT_HANDLED;
    case kBackspace: rolltui_input_erase_backward(in); return ROLLTUI_INPUT_HANDLED;
    case kDelete: rolltui_input_erase_forward(in); return ROLLTUI_INPUT_HANDLED;
    case kKillWordBack: rolltui_input_kill_word_backward(in); return ROLLTUI_INPUT_HANDLED;
    case kKillWordFwd: rolltui_input_kill_word_forward(in); return ROLLTUI_INPUT_HANDLED;
    case kKillLineStart: rolltui_input_kill_to_line_start(in); return ROLLTUI_INPUT_HANDLED;
    case kKillLineEnd: rolltui_input_kill_to_line_end(in); return ROLLTUI_INPUT_HANDLED;
    case kLeft: rolltui_input_move_left(in, 0); return ROLLTUI_INPUT_HANDLED;
    case kRight: rolltui_input_move_right(in, 0); return ROLLTUI_INPUT_HANDLED;
    case kWordLeft: rolltui_input_move_word_left(in, 0); return ROLLTUI_INPUT_HANDLED;
    case kWordRight: rolltui_input_move_word_right(in, 0); return ROLLTUI_INPUT_HANDLED;
    case kSelLeft: rolltui_input_move_left(in, 1); return ROLLTUI_INPUT_HANDLED;
    case kSelRight: rolltui_input_move_right(in, 1); return ROLLTUI_INPUT_HANDLED;
    case kSelWordLeft: rolltui_input_move_word_left(in, 1); return ROLLTUI_INPUT_HANDLED;
    case kSelWordRight: rolltui_input_move_word_right(in, 1); return ROLLTUI_INPUT_HANDLED;
    case kLineStart:
      if (in->text.empty()) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_move_line_start(in, 0);
      return ROLLTUI_INPUT_HANDLED;
    case kLineEnd:
      if (in->text.empty()) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_move_line_end(in, 0);
      return ROLLTUI_INPUT_HANDLED;
    case kSelLineStart:
      if (in->text.empty()) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_move_line_start(in, 1);
      return ROLLTUI_INPUT_HANDLED;
    case kSelLineEnd:
      if (in->text.empty()) return ROLLTUI_INPUT_IGNORED;
      rolltui_input_move_line_end(in, 1);
      return ROLLTUI_INPUT_HANDLED;
    case kUp:
      if (!rolltui_input_move_up(in, 0)) rolltui_input_history_prev(in);
      return ROLLTUI_INPUT_HANDLED;
    case kDown:
      if (!rolltui_input_move_down(in, 0)) rolltui_input_history_next(in);
      return ROLLTUI_INPUT_HANDLED;
    case kSelUp: rolltui_input_move_up(in, 1); return ROLLTUI_INPUT_HANDLED;
    case kSelDown: rolltui_input_move_down(in, 1); return ROLLTUI_INPUT_HANDLED;
    case kSelectAll: rolltui_input_select_all(in); return ROLLTUI_INPUT_HANDLED;
    case kClearSel:
      if (in->sel_empty()) return ROLLTUI_INPUT_IGNORED;
      in->sel = {};
      return ROLLTUI_INPUT_HANDLED;
    case kCopy:
      if (in->sel_empty()) return ROLLTUI_INPUT_IGNORED;
      in->fire_copy();
      return ROLLTUI_INPUT_HANDLED;
    case kEof:
      if (in->text.empty()) return ROLLTUI_INPUT_EOF;
      rolltui_input_erase_forward(in);
      return ROLLTUI_INPUT_HANDLED;
    case kUndo: return rolltui_input_undo(in) ? ROLLTUI_INPUT_HANDLED : ROLLTUI_INPUT_IGNORED;
    case kRedo: return rolltui_input_redo(in) ? ROLLTUI_INPUT_HANDLED : ROLLTUI_INPUT_IGNORED;
    default: return ROLLTUI_INPUT_IGNORED;
  }
}

unsigned char handle_mouse(RolltuiInput* in, const RolltuiMouseEvent& m, std::uint64_t now_ms) {
  using K = RolltuiMouseEvent::Kind;
  switch (m.kind) {
    case K::Press: {
      if (m.button != 1) return ROLLTUI_INPUT_IGNORED;
      std::size_t b = 0, e = 0;
      if (!in->hit(m.x, m.y, b, e)) return ROLLTUI_INPUT_IGNORED;
      in->goal_col.reset();
      in->close_group();  // a press only moves the caret/selection
      if (m.shift) {      // extend from the anchor (or the caret), the pointer's glyph included
        const std::size_t anchor = in->sel.active ? in->sel.anchor : in->caret;
        in->place(b >= anchor ? e : b, true);
        in->click = {};
        in->drag = {true, anchor, anchor, m.x, m.y};
        return ROLLTUI_INPUT_HANDLED;
      }
      const bool paired = in->click.count > 0 && now_ms >= in->click.at_ms &&
                          now_ms - in->click.at_ms <= in->opt.multi_click_ms &&
                          std::abs(m.x - in->click.x) <= 1 && m.y == in->click.y;
      in->click.count = paired ? in->click.count + 1 : 1;
      if (in->click.count > 3) in->click.count = 1;
      in->click.at_ms = now_ms;
      in->click.x = m.x;
      in->click.y = m.y;
      if (in->click.count >= 2) in->unit_around(b, in->click.count == 2, b, e);
      in->drag = {true, b, e, m.x, m.y};
      if (in->click.count >= 2 && b != e) {
        in->sel = {b, e, 1};
        in->caret = e;
      } else {
        in->sel = {};
        in->caret = b;
      }
      return ROLLTUI_INPUT_HANDLED;
    }
    case K::Drag:
      if (!in->drag.active) return ROLLTUI_INPUT_IGNORED;
      in->drag_to(m.x, m.y);
      return ROLLTUI_INPUT_HANDLED;
    case K::Release: {
      if (!in->drag.active) return ROLLTUI_INPUT_IGNORED;
      // A release where the pointer already is changes nothing.
      if (m.x != in->drag.x || m.y != in->drag.y) in->drag_to(m.x, m.y);
      in->drag.active = false;
      if (in->sel_empty()) {
        in->sel = {};
        return ROLLTUI_INPUT_HANDLED;
      }
      in->fire_copy();
      return ROLLTUI_INPUT_HANDLED;
    }
    default:
      return ROLLTUI_INPUT_IGNORED;
  }
}

}  // namespace

extern "C" unsigned char rolltui_input_handle(RolltuiInput* in, const RolltuiEvent* e,
                                              const RolltuiBindings* bindings,
                                              const RolltuiInputActions* actions, unsigned long long now_ms) {
  in->now_ms = now_ms;
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    const Snapshot pre = in->snapshot();
    in->raw_insert(std::string_view(e->text ? e->text : "", e->text_len));
    in->note_edit(true, pre);  // a paste is its own group, never merged
    return ROLLTUI_INPUT_HANDLED;
  }
  if (e->kind == ROLLTUI_EVENT_MOUSE) return handle_mouse(in, e->mouse, now_ms);
  if (e->kind == ROLLTUI_EVENT_KEY) return handle_key(in, e->key, bindings, actions);
  return ROLLTUI_INPUT_IGNORED;
}

// ---- drawing ------------------------------------------------------------------------------------------------------

extern "C" void rolltui_input_draw(const RolltuiInput* in, RolltuiFrame* f, RolltuiDrawScratch* draw,
                                   const RolltuiStyle* styles, const RolltuiInputRoles* roles, int focused) {
  in->ensure();
  const RolltuiStyle prompt = styles[static_cast<unsigned char>(in->opt.prompt_role)];
  const RolltuiStyle txt = styles[roles->text];
  const RolltuiStyle sel = styles[roles->selection];
  const RolltuiStyle ph = styles[roles->placeholder];
  const RolltuiRect a = in->area;
  const int h = std::max(a.h, 0);
  const int aw = in->opt.ambiguous_wide;
  auto visible = [&](int row) { return row >= in->top && row < in->top + h; };
  if (visible(0))
    rolltui_frame_put_text(f, draw, a.x, a.y, in->opt.prompt.p, in->opt.prompt.n, prompt, std::max(a.w, 0), aw, 0);
  if (in->text.empty() && visible(0) && in->opt.placeholder.n)
    rolltui_frame_put_text(f, draw, a.x + in->prompt_w, a.y, in->opt.placeholder.p, in->opt.placeholder.n, ph,
                           std::max(a.w - in->prompt_w, 0), aw, 0);
  const bool has_sel = !in->sel_empty();
  const std::size_t sb = in->sel_begin(), se = in->sel_end();
  for (std::size_t i = 0; i < in->g.size(); ++i) {
    const FlowCell& c = in->flow.cells[i];
    if (!visible(c.row)) continue;
    const int y = a.y + (c.row - in->top);
    const int x = a.x + c.col;
    const std::size_t off = in->g[i].offset;
    const bool in_sel = has_sel && off >= sb && off < se;
    const RolltuiStyle st = in_sel ? sel : txt;
    const char c0 = in->text[off];
    if (c0 == '\n') {  // a selected newline shows as one highlighted cell
      if (in_sel && c.col < a.w) rolltui_frame_put(f, x, y, " ", 1, 1, st, 0);
      continue;
    }
    if (c0 == '\t') {
      for (int k = 0; k < c.width && c.col + k < a.w; ++k) rolltui_frame_put(f, x + k, y, " ", 1, 1, st, 0);
      continue;
    }
    if (c.width <= 0 || c.col + c.width > a.w) continue;  // nothing to draw, or the area's edge
    rolltui_frame_put(f, x, y, in->text.data() + off, in->g[i].length, c.width, st, 0);
  }
  if (focused) {
    int row = 0, col = 0;
    in->cell_of(in->caret, row, col);
    if (visible(row))
      rolltui_frame_set_cursor(f, a.x + std::min(col, std::max(a.w - 1, 0)), a.y + (row - in->top), 1);
  }
}
