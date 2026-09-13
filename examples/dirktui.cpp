//
// rolltui/examples/dirktui.cpp — `dirktui`, a directory picker for the shell; `dirk` is its shell function.
//
// It is a READ-ONLY file-system browser in the shape macOS calls column view (Miller columns):
// side-by-side lists, the selection in a column filling the one to its right, a horizontal
// scroll when the path is deeper than the window, and a vertical scroll per column. It never
// writes, renames, moves or deletes anything. Reading a directory is the LIBRARY's
// (`rolltui_dir_read`), so this file's whole remaining contact with the file system is one `stat`
// asking whether a typed path is a directory before jumping to it.
//
// ============================================================================================
// THE CONTRACT WITH THE SHELL, which is the whole reason it is a utility and not a demo
//
//   * It DRAWS on /dev/tty and ANSWERS on stdout, always. A shell function captures stdout with
//     `$(dirktui)`, so no frame may ever reach it; the terminal is opened by name rather than
//     inherited. fzf does the same, for the same reason.
//   * Enter ACCEPTS the selected entry: its path is printed on stdout, one line, and the exit
//     status is 0. A selected file is printed as itself — the shell side decides that a file
//     means its directory, because that is a decision about `cd`, not about browsing.
//   * Escape (or Ctrl-Q / Ctrl-C anywhere) CANCELS: nothing is printed and the exit status is 1.
//     "Nothing printed" and "exit 0" never coincide, because `cd ""` is `cd ~`, silently.
//   * `dirktui init zsh|bash|fish` prints the shell integration: a `dirk` function that browses
//     then goes where you chose, and a Right Arrow binding that opens the browser only when the
//     cursor is already at the end of the line. Usage/no-terminal is exit 2.
//
// ============================================================================================
// WHAT IS THE APP'S AND WHAT IS THE SCREEN'S
//
// The screen is FILES (`examples/presets/`): the layout names the windows and DECLARES every
// action, the bindings file says which chord runs each one, and the menu is a file too. No
// string below names a window id, and `dirktui_test`'s grep asserts it — the same control
// `files_only_test` runs for the studio.
//
// The BROWSER OWNS ITS MODEL, deliberately. It is the library's aligned probe: a widget with
// data and structure of its own — children, two scroll axes, a selection that propagates
// sideways, a width that depends on its contents — which is the case a two-callback adapter
// cannot serve. Every place the public header could not do something is a wall to record.
//
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"

// ADDITIVE, NOT SUBTRACTIVE. `dirk` is the product and cannot drive itself: the
// script vocabulary is not compiled into it. `dirk-selftest` is the same source plus
// this, which is what a golden frame is rendered by. The app code either binary runs is the
// same code, so the product binary is the one the app was verified through.
#ifdef ROLLTUI_SELFTEST
#include "rolltui/selftest/script.hpp"
#endif

namespace {

constexpr const char* kBrowserKind = "browser";
constexpr const char* kBrowserDescribes = "a column view of a directory tree";

// ---- what the host and the widget agree on ------------------------------------------------
// One struct, owned by the app, BORROWED by the widget through its factory ctx. This is how a
// host kind reaches the host's own state — the shape paint uses for its brush, and the reason
// `rolltui_windows_bindings` being INTERNAL costs nothing: a host already holds its own table
// and hands it over here (wall 1 in the phase file).
enum class Sort { Name, Size, Modified };

struct Options {
  bool hidden = false;
  Sort sort = Sort::Name;
  const RolltuiBindings* bindings = nullptr;  // BORROWED: the app's live table
};

// A `RolltuiStr` as a `std::string`, at the sites that want one. The library's own vocabulary
// never names a std:: type, so a host that composes with std::string converts here — one line,
// judged per call site, which is the boundary rule rather than a wrapper around the API.
std::string str_of(const RolltuiStr& s) { return std::string(s.p ? s.p : "", s.n); }

// NO `Entry` OF ITS OWN. `RolltuiDirEntry` carries exactly what this app kept — the name, whether
// it is a directory, whether it could be described, its size, its time and its mode — so a
// parallel struct would be a second thing to drift and a copy per directory to keep it in step.

// ---- text measured and cut to a column's width ---------------------------------------------
// "HOW MANY BYTES OF THIS FIT IN N CELLS" is `rolltui_u_fit`, and it is public because every
// list, tree, table and column view truncates and would otherwise write this loop itself.
// `rolltui_frame_put_text` computes exactly that offset to honour `max_cells`; returning only
// the count leaves a caller placing an ellipsis at a cut it has to find some other way — by
// driving the WRAP ENGINE as a grapheme iterator, which is public, correct and indirect.
struct Measure {
  RolltuiUnicodeScratch* u = rolltui_u_scratch_new();
  ~Measure() { rolltui_u_scratch_free(u); }
  int width(const std::string& s) { return rolltui_u_display_width(u, s.data(), s.size(), 0); }
  // The longest prefix of `s` that fits in `cells`, as a byte count.
  std::size_t prefix_bytes(const std::string& s, int cells) {
    return rolltui_u_fit(u, s.data(), s.size(), cells, 0, nullptr);
  }

  // The byte offset that drops AT LEAST `cells` cells from the left of `s`, and how many cells
  // were actually dropped (one more than asked when a wide glyph straddles the cut — a glyph
  // is never split, so the cut lands after it). For the column half-scrolled off the left edge.
  std::size_t skip_bytes(const std::string& s, int cells, int& dropped) {
    std::size_t at = prefix_bytes(s, cells);
    dropped = width(s.substr(0, at));
    if (dropped < cells && at < s.size()) {
      at = prefix_bytes(s, cells + 1);
      dropped = width(s.substr(0, at));
    }
    return at;
  }

  // `s`, or a prefix of it with a single-cell ellipsis, fitting `cells`.
  std::string fit(const std::string& s, int cells) {
    if (cells <= 0) return std::string();
    if (width(s) <= cells) return s;
    const std::size_t keep = prefix_bytes(s, cells - 1);
    return s.substr(0, keep) + "\xE2\x80\xA6";  // U+2026
  }
};

// ---- one column: a directory, its entries, and where the eye is -----------------------------
struct Column {
  std::string dir;
  RolltuiDirList entries{};  // OWNED; released in the destructor
  std::size_t sel = 0;
  std::size_t top = 0;   // the first visible row: this column's own vertical scroll
  int width = 18;        // derived from the content, clamped
  std::size_t hidden_n = 0;
  std::string error;     // opendir failed: a NOTE, never a crash
  Column() = default;
  Column(const Column&) = delete;
  Column& operator=(const Column&) = delete;
  Column(Column&& o) noexcept { *this = std::move(o); }
  Column& operator=(Column&& o) noexcept {
    if (this != &o) {
      rolltui_dir_list_release(&entries);
      dir = std::move(o.dir); entries = o.entries; sel = o.sel; top = o.top;
      width = o.width; hidden_n = o.hidden_n; error = std::move(o.error);
      o.entries = RolltuiDirList{};
    }
    return *this;
  }
  ~Column() { rolltui_dir_list_release(&entries); }
};

// THE READ IS THE LIBRARY'S. What is left here is this app's own two choices: it describes the
// LINK rather than what it points at, because a browser shows what is on disk, and it keeps the
// count of what it hid so the status line can say so.
bool read_dir(const std::string& path, const Options& opt, Column& out) {
  RolltuiStr err{};
  const int flags = (opt.hidden ? ROLLTUI_DIR_HIDDEN : 0) | ROLLTUI_DIR_LINKS;
  const int sort = opt.sort == Sort::Size      ? ROLLTUI_SORT_SIZE
                   : opt.sort == Sort::Modified ? ROLLTUI_SORT_MODIFIED
                                                : ROLLTUI_SORT_NAME;
  out.error.clear();
  const bool ok = rolltui_dir_read(path.data(), path.size(), sort, flags, &out.entries, &err) != 0;
  if (!ok) out.error.assign(err.p ? err.p : "", err.n);
  out.hidden_n = out.entries.hidden_n;
  rolltui_str_free(&err);
  return ok;
}

// ---- the widget ----------------------------------------------------------------------------
struct Browser {
  std::string source;
  const Options* opt = nullptr;        // BORROWED
  RolltuiWindows* windows = nullptr;   // BORROWED: where `draw` asks for the style table
  RolltuiDrawScratch* draw_scratch = rolltui_draw_scratch_new();
  Measure measure;
  RolltuiRect inner{};
  std::vector<Column> cols;
  std::size_t focus_col = 0;
  std::string root;
  // THE HORIZONTAL SCROLL IS ONE NUMBER: where column 0's left edge sits relative to the inner
  // rect, in cells, never positive. The ANCHOR RULE picks its target: when every column fits,
  // nothing scrolls and the columns pack from the left; otherwise the column to the RIGHT of the
  // focused one — the one the selection is filling — ends exactly at the right edge, so the eye
  // always has the focused column and its whole preview, and whatever fits to the left is shown
  // partially rather than not at all. A change of target is ANIMATED, quickly (`kScrollMs`): the
  // columns slide to where they belong, so the eye follows a column rather than re-finding it.
  int scroll_x = 0;
  int scroll_target = 0;
  int scroll_from = 0;
  unsigned long long scroll_start_ms = 0;
  bool scrolling = false;
  unsigned long long now_ms = 0;  // the frame clock, set by the app before each frame; 0 = headless, no motion
  static constexpr unsigned long long kScrollMs = 120;
  // THE OUTCOME. Set by an action, read by the app once the poll's events are handled. The widget
  // cannot end the process and must not decide what a chosen path MEANS — printing it, and what
  // the shell does with it, are the app's and the shell's. Two flags rather than one enum with an
  // "open" state, so a reader of either asks one question.
  bool accepted = false;
  bool cancelled = false;

  const Column* focused() const { return focus_col < cols.size() ? &cols[focus_col] : nullptr; }
  Column* focused() { return focus_col < cols.size() ? &cols[focus_col] : nullptr; }

  const RolltuiDirEntry* selected() const {
    const Column* c = focused();
    return c && c->sel < c->entries.n ? &c->entries.v[c->sel] : nullptr;
  }
  std::string selected_path() const {
    const Column* c = focused();
    if (!c || c->sel >= c->entries.n) return c ? c->dir : root;
    return c->dir == "/" ? "/" + str_of(c->entries.v[c->sel].name)
                         : c->dir + "/" + str_of(c->entries.v[c->sel].name);
  }

  void set_root(const std::string& path) {
    root = path;
    cols.clear();
    focus_col = 0;
    Column c;
    c.dir = path;
    read_dir(path, *opt, c);
    measure_width(c);
    cols.push_back(std::move(c));
    open_selected();
    retarget();
  }

  void reload() {
    // Re-read every column in place, keeping the selection BY NAME rather than by index, so a
    // sort change or a dotfile toggle does not move the eye to a different file.
    std::vector<std::string> keep;
    for (const Column& c : cols) keep.push_back(c.sel < c.entries.n ? str_of(c.entries.v[c.sel].name) : std::string());
    for (std::size_t i = 0; i < cols.size(); ++i) {
      read_dir(cols[i].dir, *opt, cols[i]);
      measure_width(cols[i]);
      cols[i].sel = 0;
      for (std::size_t j = 0; j < cols[i].entries.n; ++j)
        if (str_of(cols[i].entries.v[j].name) == keep[i]) cols[i].sel = j;
      clamp_scroll(cols[i]);
    }
  }

  void measure_width(Column& c) {
    int longest = 0;
    for (std::size_t i = 0; i < c.entries.n; ++i)
      longest = std::max(longest, measure.width(str_of(c.entries.v[i].name)) + (c.entries.v[i].is_dir ? 2 : 0));
    c.width = std::min(28, std::max(12, longest + 2));
  }

  // The column to the right of the focused one exists exactly when a directory is selected.
  void open_selected() {
    cols.resize(focus_col + 1);
    const RolltuiDirEntry* e = selected();
    if (!e || !e->is_dir) return;
    Column c;
    c.dir = selected_path();
    read_dir(c.dir, *opt, c);
    measure_width(c);
    cols.push_back(std::move(c));
  }

  int rows_visible() const { return inner.h > 1 ? inner.h - 1 : (inner.h > 0 ? inner.h : 0); }

  void clamp_scroll(Column& c) {
    const int vis = rows_visible();
    if (vis <= 0) {
      c.top = 0;
      return;
    }
    if (c.sel < c.top) c.top = c.sel;
    if (c.sel >= c.top + static_cast<std::size_t>(vis)) c.top = c.sel - static_cast<std::size_t>(vis) + 1;
    if (c.entries.n <= static_cast<std::size_t>(vis)) c.top = 0;
  }

  // Column `ci`'s left edge, in the inner rect's x, at the CURRENT scroll (mid-slide included).
  int column_x(std::size_t ci) const {
    int x = inner.x + scroll_x;
    for (std::size_t j = 0; j < ci && j < cols.size(); ++j) x += cols[j].width + 1;
    return x;
  }

  // The anchor rule, as a target. Called after anything that changes which columns exist, which
  // is focused, or how wide the rect is; the slide toward it is `advance()`'s.
  void retarget() {
    int total = 0;
    for (const Column& c : cols) total += c.width + 1;
    total = total > 0 ? total - 1 : 0;
    int target = 0;
    if (total > inner.w && !cols.empty()) {
      const std::size_t last = std::min(focus_col + 1, cols.size() - 1);
      int right_end = 0;
      for (std::size_t j = 0; j <= last; ++j) right_end += cols[j].width + 1;
      right_end -= 1;
      target = std::min(0, inner.w - right_end);
      // A focused column wider than what is left of the window still starts on screen.
      int focus_start = 0;
      for (std::size_t j = 0; j < focus_col; ++j) focus_start += cols[j].width + 1;
      if (focus_start + target < 0 && cols[focus_col].width >= inner.w) target = -focus_start;
    }
    if (target == scroll_target) return;
    scroll_target = target;
    if (now_ms == 0) { scroll_x = target; scrolling = false; return; }  // headless: no motion
    scroll_from = scroll_x;
    scroll_start_ms = now_ms;
    scrolling = true;
  }

  // Where the slide has got to at `now_ms`. Ease-out: fast away from where it was, settling
  // into place, which reads as "the columns moved" rather than "the screen jumped".
  void advance() {
    if (!scrolling) return;
    const unsigned long long t = now_ms >= scroll_start_ms ? now_ms - scroll_start_ms : kScrollMs;
    if (t >= kScrollMs || now_ms == 0) { scroll_x = scroll_target; scrolling = false; return; }
    const double u = static_cast<double>(t) / static_cast<double>(kScrollMs);
    const double eased = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u);
    scroll_x = scroll_from + static_cast<int>(std::lround((scroll_target - scroll_from) * eased));
  }

  void move(int delta) {
    Column* c = focused();
    if (!c || c->entries.n == 0) return;
    long long at = static_cast<long long>(c->sel) + delta;
    at = std::max<long long>(0, std::min<long long>(at, static_cast<long long>(c->entries.n) - 1));
    c->sel = static_cast<std::size_t>(at);
    clamp_scroll(*c);
    open_selected();
    retarget();
  }
  void select(std::size_t i) {
    Column* c = focused();
    if (!c || i >= c->entries.n) return;
    c->sel = i;
    clamp_scroll(*c);
    open_selected();
    retarget();
  }
  void into() {
    const RolltuiDirEntry* e = selected();
    if (!e || !e->is_dir) return;
    open_selected();
    if (focus_col + 1 < cols.size()) {
      ++focus_col;
      open_selected();  // the NEW focus's own preview, so the column to its right is never empty
      retarget();
    }
  }
  void out() {
    if (focus_col > 0) {
      --focus_col;
      cols.resize(focus_col + 2 <= cols.size() ? focus_col + 2 : cols.size());
      retarget();
      return;
    }
    // At the leftmost column, going out means the parent directory becomes the new root — the
    // one place this app follows a path upward, and it never leaves the file system's root.
    const std::size_t slash = root.find_last_of('/');
    if (root == "/" || slash == std::string::npos) return;
    const std::string child = root.substr(slash + 1);
    const std::string parent = slash == 0 ? "/" : root.substr(0, slash);
    set_root(parent);
    Column* c = focused();
    if (!c) return;
    for (std::size_t i = 0; i < c->entries.n; ++i)
      if (str_of(c->entries.v[i].name) == child) c->sel = i;
    clamp_scroll(*c);
    open_selected();
    retarget();
  }
};

void browser_destroy(void* ctx) {
  Browser* b = static_cast<Browser*>(ctx);
  rolltui_draw_scratch_free(b->draw_scratch);
  delete b;
}

// `problem()` answers ONE question: what does this kind NEED that the app has not provided. Its
// reader is whoever builds the app, and it feeds the end-of-init gap report, whose whole sentence
// is "this screen names N things this app must provide". A directory that does not exist is not
// something the app failed to provide — it is DATA, and a person who mistyped a path is not the
// audience for a sentence about what an app must provide. That failure is drawn in the panel and
// said in the status line, where the person who caused it is looking.
int browser_problem(void* ctx, RolltuiStr* out) {
  const Browser* b = static_cast<const Browser*>(ctx);
  if (!b->cols.empty()) return 0;
  const std::string why = "nothing is bound to '" + b->source + "'";
  rolltui_str_set(out, why.data(), why.size());
  return 1;
}

// Notes do NOT stop the widget drawing — the dotfile count is exactly that kind of remark.
int browser_note_at(void* ctx, std::size_t i, RolltuiStr* out) {
  const Browser* b = static_cast<const Browser*>(ctx);
  if (i != 0) return 0;
  const Column* c = b->focused();
  if (!c || c->hidden_n == 0) return 0;
  const std::string s = std::to_string(c->hidden_n) + " hidden";
  rolltui_str_set(out, s.data(), s.size());
  return 1;
}

void browser_layout(void* ctx, const RolltuiResolvedNode* rn) {
  Browser* b = static_cast<Browser*>(ctx);
  rolltui_content_rect(rn, &b->inner);
  for (Column& c : b->cols) b->clamp_scroll(c);
  b->retarget();
  b->advance();
}

void browser_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  Browser* b = static_cast<Browser*>(ctx);
  RolltuiRect r{};
  rolltui_content_rect(rn, &r);
  if (r.w <= 0 || r.h <= 0) return;  // the standing rule: every view shrinks to nothing gracefully
  const RolltuiStyle* styles = rolltui_windows_styles(b->windows);
  auto S = [&](unsigned char role) { return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role); };
  const RolltuiStyle text = S(ROLLTUI_ROLE_TEXT);
  const RolltuiStyle dim = S(ROLLTUI_ROLE_TEXT_MUTED);
  const RolltuiStyle head = S(ROLLTUI_ROLE_LABEL);
  const RolltuiStyle here = S(ROLLTUI_ROLE_MENU_SELECTED);
  const RolltuiStyle trail = S(ROLLTUI_ROLE_SELECTION);

  for (std::size_t ci = 0; ci < b->cols.size(); ++ci) {
    const Column& c = b->cols[ci];
    const int x = b->column_x(ci);
    if (x >= r.x + r.w) break;
    if (x + c.width <= r.x) continue;  // wholly off the left edge
    const int cw = std::min(c.width, r.x + r.w - x);
    if (cw <= 0) break;
    // A column partly off the LEFT edge shows its right part: `hidden` cells of every line are
    // dropped, never drawn outside the rect. The title starts at x; the rows at x + 1.
    const int hidden = r.x - x;  // ≤ 0 when the column starts on screen
    auto put_clipped = [&](int at, int y, const std::string& text, const RolltuiStyle& st, int room) {
      const int drop = r.x - at;
      if (drop <= 0) { rolltui_frame_put_text(f, b->draw_scratch, at, y, text.data(), text.size(), st, room, 0, 0); return; }
      if (drop >= room) return;
      int dropped = 0;
      const std::size_t from = b->measure.skip_bytes(text, drop, dropped);
      rolltui_frame_put_text(f, b->draw_scratch, at + dropped, y, text.data() + from, text.size() - from, st,
                             room - dropped, 0, 0);
    };
    // The column's own head: the directory's last component, so a deep path stays readable.
    const std::size_t slash = c.dir.find_last_of('/');
    const std::string title = c.dir == "/" ? "/" : c.dir.substr(slash == std::string::npos ? 0 : slash + 1);
    put_clipped(x, r.y, b->measure.fit(title, cw), head, cw);
    const int rows = b->rows_visible();
    for (int row = 0; row < rows; ++row) {
      const std::size_t i = c.top + static_cast<std::size_t>(row);
      const int y = r.y + 1 + row;
      if (y >= r.y + r.h) break;
      if (i >= c.entries.n) break;
      const RolltuiDirEntry& e = c.entries.v[i];
      const bool is_sel = i == c.sel;
      const bool is_focus_col = ci == b->focus_col;
      const RolltuiStyle st = is_sel ? (is_focus_col ? here : trail) : (e.is_dir ? text : (e.unreadable ? dim : text));
      if (is_sel) {
        const int fx = std::max(x, r.x);
        rolltui_frame_fill(f, b->draw_scratch, RolltuiRect{fx, y, x + cw - fx, 1}, st, nullptr, 0);
      }
      // A directory is marked with a trailing chevron rather than a colour, so the shape
      // survives `mono` and a colour-blind reader alike.
      const std::string label = str_of(e.name) + (e.is_dir ? " \xE2\x80\xBA" : "");
      put_clipped(x + 1, y, b->measure.fit(label, cw - 1), st, cw - 1);
    }
    if (c.entries.n == 0) {
      // A DIRECTORY THAT COULD NOT BE OPENED MUST NOT LOOK LIKE AN EMPTY ONE. Both have no
      // entries, and drawing "(empty)" for both is a wrong answer that reports itself as a
      // success. The widget draws this itself rather than leaving it to the library's error
      // panel, because that panel is driven by `problem()`, whose reader is the app's author.
      // ROW r.y + 1 IS NOT ALWAYS INSIDE THIS RECT. A column one row tall has no second row, and
      // writing to it lands on whatever is drawn below — a neighbour's border. Every view here has
      // to survive being one cell.
      if (r.h > 1) {
        // AN ERROR IS NOT A FILENAME AND DOES NOT RESPECT THE COLUMN GRID. A column is sized for
        // names, so "cannot open /very/long/path" truncates to "cannot ope…" and tells nobody
        // anything. It gets the rest of the panel instead, which is space no name needed.
        const bool failed = !c.error.empty();
        const std::string say = failed ? c.error : std::string("(empty)");
        const RolltuiStyle es = failed ? S(ROLLTUI_ROLE_ERROR) : dim;
        const int room = failed ? (r.x + r.w - (x + 1)) : (cw - 1);
        put_clipped(x + 1, r.y + 1, b->measure.fit(say, room), es, room);
      }
    }
    (void)hidden;
  }
}

// Two axes, which is what a column view needs and what the slot's `axis` parameter is for.
int browser_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  const Browser* b = static_cast<const Browser*>(ctx);
  if (axis == ROLLTUI_AXIS_VERTICAL) {
    const Column* c = b->focused();
    if (!c) return 0;
    out->first = c->top;
    out->visible = static_cast<std::size_t>(b->rows_visible());
    out->total = c->entries.n;
    return 1;
  }
  std::size_t first = 0, visible = 0;
  for (std::size_t ci = 0; ci < b->cols.size(); ++ci) {
    const int x = b->column_x(ci);
    if (x + b->cols[ci].width <= b->inner.x) { first = ci + 1; continue; }
    if (x >= b->inner.x && x + b->cols[ci].width <= b->inner.x + b->inner.w) ++visible;
  }
  out->first = first;
  out->visible = visible > 0 ? visible : 1;
  out->total = b->cols.size();
  return 1;
}

int browser_scroll_to(void* ctx, unsigned char axis, std::size_t first) {
  Browser* b = static_cast<Browser*>(ctx);
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  Column* c = b->focused();
  if (!c) return 0;
  const int vis = b->rows_visible();
  const std::size_t max_top = c->entries.n > static_cast<std::size_t>(vis)
                                  ? c->entries.n - static_cast<std::size_t>(vis)
                                  : 0;
  c->top = first > max_top ? max_top : first;  // rule 4: anything that accepts must CLAMP
  return 1;
}

int browser_handle(void* ctx, const RolltuiEvent* e) {
  Browser* b = static_cast<Browser*>(ctx);
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    using K = RolltuiMouseEvent::Kind;
    const K k = e->mouse.kind;
    if (k == K::WheelUp || k == K::WheelDown) {
      Column* c = b->focused();
      if (!c) return 0;
      const long long step = k == K::WheelUp ? -3 : 3;
      const long long top = static_cast<long long>(c->top) + step;
      return browser_scroll_to(ctx, ROLLTUI_AXIS_VERTICAL, static_cast<std::size_t>(std::max<long long>(0, top)));
    }
    if (k != K::Press) return 0;
    // Which column was clicked, and which row in it — the widget's own hit test, because the
    // columns are its structure and the library has no way to know about them.
    for (std::size_t ci = 0; ci < b->cols.size(); ++ci) {
      const int x = b->column_x(ci);
      const int cw = b->cols[ci].width;
      if (e->mouse.x >= x && e->mouse.x < x + cw && e->mouse.x >= b->inner.x) {
        b->focus_col = ci;
        b->cols.resize(ci + 1);
        const int row = e->mouse.y - b->inner.y - 1;
        if (row >= 0) b->select(b->cols[ci].top + static_cast<std::size_t>(row));
        else b->open_selected();
        b->retarget();
        return 1;
      }
    }
    return 0;
  }
  if (e->kind != ROLLTUI_EVENT_KEY) return 0;
  // THE KEYS ARE THE BINDINGS FILE'S. A host kind resolves its own scope from the table the
  // app holds and lends through the factory ctx — see wall 1: `rolltui_windows_bindings` is
  // INTERNAL, and it turns out not to be needed, because the host already owns the table.
  if (!b->opt->bindings) return 0;
  std::size_t len = 0;
  const char* a = rolltui_bindings_action_for(b->opt->bindings, &e->key, kBrowserKind, std::strlen(kBrowserKind), &len);
  if (!a || len == 0) return 0;
  const std::string action(a, len);
  const int page = std::max(1, b->rows_visible() - 1);
  if (action == "browser.up") b->move(-1);
  else if (action == "browser.down") b->move(1);
  else if (action == "browser.page_up") b->move(-page);
  else if (action == "browser.page_down") b->move(page);
  else if (action == "browser.first") b->select(0);
  else if (action == "browser.last") { const Column* c = b->focused(); if (c && c->entries.n != 0) b->select(c->entries.n - 1); }
  else if (action == "browser.into") b->into();
  else if (action == "browser.out") b->out();
  else if (action == "browser.accept") b->accepted = true;
  else if (action == "browser.cancel") b->cancelled = true;
  else return 0;
  return 1;
}

constexpr RolltuiWidgetPlugin kBrowserPlugin = {
    /*destroy=*/browser_destroy,
    /*layout=*/browser_layout,
    /*draw=*/browser_draw,
    /*problem=*/browser_problem,
    /*note_at=*/browser_note_at,
    /*desired_outer=*/nullptr,
    /*handle=*/browser_handle,
    /*scroll_extent=*/browser_scroll_extent,
    /*scroll_to=*/browser_scroll_to,
};

struct BrowserFactoryCtx {
  const Options* opt;
  RolltuiWindows* windows;
  const std::string* root;
};

RolltuiWidget browser_factory(void* ctx, RolltuiWindows* /*w*/, const char* content, size_t len) {
  const BrowserFactoryCtx* fc = static_cast<const BrowserFactoryCtx*>(ctx);
  const char* source = nullptr;
  std::size_t source_len = 0;
  RolltuiStr why{};
  unsigned char problem = 0;
  if (!rolltui_content_parse(rolltui_windows_context(fc->windows), content, len, nullptr, nullptr, nullptr, nullptr,
                             &source, &source_len, &problem, &why))
    return RolltuiWidget{};
  Browser* b = new Browser();
  b->source.assign(source, source_len);
  b->opt = fc->opt;
  b->windows = fc->windows;
  b->set_root(*fc->root);
  return RolltuiWidget{&kBrowserPlugin, b};
}

}  // namespace

namespace {

// ---- the app ---------------------------------------------------------------------------
// APP LIFETIME, RELEASED IN ONE DESTRUCTOR — paint's shape, and for its reason: none of these
// is per-frame, so no wrapper type earns its place. A missed release leaks once and
// `rolltui_shutdown`'s `live_bytes == 0` is what catches it.
struct App {
  RolltuiContext* ctx = rolltui_context_new();  // OWNED: this app's session (Phase 25)
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  RolltuiDrawScratch* draw_scratch = rolltui_draw_scratch_new();
  RolltuiBindings* bindings = rolltui_bindings_clone(rolltui_bindings_default(ctx));
  RolltuiWindows* windows = rolltui_windows_new(ctx);
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  RolltuiComposeScratch* compose_scratch = rolltui_compose_scratch_new();
  RolltuiLayout* layout = nullptr;  // OWNED (Phase 23: a layout is a handle)
  // OWNED: the user's own theme and key presets. An app that cannot change how it looks is an
  // app the library's editors have nothing to edit — the kinds are the library's, the STORE is
  // what makes them this app's.
  RolltuiPresetStore* theme_store = nullptr;
  RolltuiPresetStore* keys_store = nullptr;
  unsigned long long theme_seen = 0;
  Options opt;
  BrowserFactoryCtx factory_ctx{};
  std::string root;
  std::string note;   // the library's own report for this frame
  std::string hint;   // this app's own last word (a bad path, a jump)
  // CALLER-FILLED, one per run: the status line's fields, reset and refilled every frame so
  // the array and each row's buffer are reused rather than rebuilt.
  RolltuiRows status_rows{};
  int w = 100, h = 30;
  bool quit = false;
  unsigned long long now_ms = 0;  // the frame clock; 0 in a headless frame, where nothing moves
  // How soon this frame wants redrawing: a sliding column asks for the next tick, else `idle`.
  int poll_timeout_ms(int idle) {
    Browser* b = browser();
    return b && b->scrolling ? 16 : idle;
  }
  // THE EXIT CONTRACT: `chosen` is printed and the status is 0 ONLY when an accept happened.
  // Every other way out — cancel, a global quit — prints nothing and exits 1. The shell function
  // reads exactly this pair, and an empty path with status 0 would send it to `cd ""`.
  std::string chosen;
  int exit_code = 1;

  App() {
    layout = rolltui_layout_new();
    rolltui_context_set_library_defaults(ctx);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() {
    rolltui_preset_store_free(keys_store);
    rolltui_preset_store_free(theme_store);
    rolltui_rows_release(&status_rows);
    rolltui_layout_free(layout);
    rolltui_compose_scratch_free(compose_scratch);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_bindings_free(bindings);
    rolltui_draw_scratch_free(draw_scratch);
    rolltui_effect_map_free(effects);
    rolltui_context_free(ctx);  // LAST: the registries every handle above resolved through
  }

  RolltuiRect area() const { return {0, 0, w, h > 1 ? h - 1 : 0}; }
  const RolltuiStyle& style(unsigned char role) const {
    return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role);
  }
  void set_theme(const char* name) {
    rolltui_effect_map_free(effects);
    effects = rolltui_theme_builtin_fill(name, std::strlen(name), styles, ROLLTUI_ROLE_COUNT);
  }

  // The look comes from the STORE once there is one, so an edit made in the theme editor is what
  // the next frame draws. Falls back to the built-in when the store has nothing loadable, which
  // is what keeps a broken preset directory from being a blank screen.
  void sync_theme() {
    if (!theme_store) return;
    RolltuiThemePresetValue* w = (RolltuiThemePresetValue*)rolltui_preset_store_working(theme_store);
    if (!w) return;
    RolltuiThemeReport rep{};
    RolltuiStyle got[ROLLTUI_ROLE_COUNT]{};
    RolltuiStr name{};
    // "auto" is not a mode, so it resolves to dark here; a terminal probe would do better and
    // this app does not have one yet.
    const int named = rolltui_theme_mode_from_name(w->mode.p ? w->mode.p : "", w->mode.n);
    const int mode = named >= 0 ? named : ROLLTUI_MODE_DARK;
    RolltuiEffectMap* eff = rolltui_theme_load(w->colours, mode, rolltui_theme_default_vocab(), got, &name, &rep);
    if (eff) {
      std::copy(std::begin(got), std::end(got), styles);
      rolltui_effect_map_free(effects);
      effects = eff;
      RolltuiScrollbarGlyphs g;
      rolltui_theme_scrollbar_glyphs(w->colours, &g);
      rolltui_context_set_scrollbar_glyphs(ctx, &g);
    }
    rolltui_str_free(&name);
    rolltui_theme_report_release(&rep);
    rolltui_preset_store_value_free(theme_store, w);
  }

  // the kind table belongs to a CONTEXT, so this registers into this app's session.
  void register_browser_kind() {
    rolltui_widget_kind_register(ctx, kBrowserKind, std::strlen(kBrowserKind), ROLLTUI_SOURCE_REQUIRED,
                                 kBrowserDescribes, std::strlen(kBrowserDescribes));
  }

  // WALL 3 (phase file): reaching one's OWN widget means composing the content string the
  // library keyed it under and comparing the plugin pointer. That is paint's `canvas()` almost
  // verbatim — a second consumer writing the same wrapper, which is rule 5's tell.
  Browser* browser() {
    RolltuiStr content{};
    rolltui_content_format(kBrowserKind, std::strlen(kBrowserKind), "tree", 4, ROLLTUI_SOURCE_REQUIRED, &content);
    RolltuiWidget* wi = rolltui_windows_widget_for(windows, content.c_str(), content.size());
    rolltui_str_free(&content);
    return wi && wi->vt == &kBrowserPlugin ? static_cast<Browser*>(wi->ctx) : nullptr;
  }

  static const std::vector<std::string>& help_scopes() {
    static const std::vector<std::string> s = {"app", "browser", "input", "stack"};
    return s;
  }

  void mount() {
    opt.bindings = bindings;
    factory_ctx = {&opt, windows, &root};
    register_browser_kind();
    rolltui_context_register_kind(ctx, kBrowserKind, std::strlen(kBrowserKind), browser_factory, &factory_ctx,
                                  nullptr);
    rolltui_windows_bind_rows(windows, "entry", 5, entry_rows, this, nullptr);
    rolltui_windows_bind_submit(windows, "path", 4, on_submit, this, nullptr, /*on_submit=*/0);
    rolltui_windows_bind_note(windows, "path", 4, path_note, this, nullptr);
    rolltui_context_set_help(ctx, "", 0, "", 0);
    rolltui_context_clear_help_scopes(ctx);
    for (const std::string& s : help_scopes()) rolltui_context_add_help_scope(ctx, s.data(), s.size());
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    std::size_t an = 0;
    const RolltuiLayoutAction* av = rolltui_layout_actions(layout, &an);
    rolltui_bindings_declare(bindings, av, an, nullptr, 0);
  }

  // `rows:entry` — what is known about the selection. A popup the LAYOUT declares, filled by
  // an ordinary rows source, so the details page needs no host-side window code at all.
  static void entry_rows(void* ctx, RolltuiRows* out) {
    App& a = *static_cast<App*>(ctx);
    Browser* b = a.browser();
    const RolltuiDirEntry* e = b ? b->selected() : nullptr;
    if (!e) {
      rolltui_rows_add(out, "entry", 5, "(none)", 6);
      return;
    }
    const std::string path = b->selected_path();
    rolltui_rows_add(out, "name", 4, e->name.p ? e->name.p : "", e->name.n);
    rolltui_rows_add(out, "folder", 6, path.data(), path.size());
    const char* kind = e->unreadable ? "unreadable" : e->is_dir ? "directory" : "file";
    rolltui_rows_add(out, "kind", 4, kind, std::strlen(kind));
    const std::string size = e->is_dir ? std::string("-") : human_size(e->size);
    rolltui_rows_add(out, "size", 4, size.data(), size.size());
    const std::string when = stamp(e->modified);
    rolltui_rows_add(out, "modified", 8, when.data(), when.size());
    const std::string perm = permissions(e->mode);
    rolltui_rows_add(out, "mode", 4, perm.data(), perm.size());
  }

  // A bad path is a NAMED problem in the input's own note, which is the library's standard for
  // a source that cannot do what was asked — never a crash, never silence.
  static void path_note(void* ctx, RolltuiNote* out) {
    App& a = *static_cast<App*>(ctx);
    if (!a.hint.empty()) { out->set(a.hint.data(), a.hint.size()); return; }
    const char* idle = "type a path and press Enter";
    out->set(idle, std::strlen(idle));
  }

  static void on_submit(void* ctx, const char* text, std::size_t len) {
    App& a = *static_cast<App*>(ctx);
    a.jump(std::string(text, len));
  }

  void jump(std::string path) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    if (path.empty()) { hint = "a path, please"; return; }
    if (path[0] == '~') { const char* home = std::getenv("HOME"); path = (home ? home : "") + path.substr(1); }
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) { hint = "no such path: " + path; return; }
    if (!S_ISDIR(st.st_mode)) { hint = "not a directory: " + path; return; }
    if (Browser* b = browser()) {
      root = path;
      b->set_root(path);
      hint = "at " + path;
    }
  }

  static std::string human_size(long long n) {
    static const char* unit[] = {"B", "K", "M", "G", "T"};
    double v = static_cast<double>(n);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[32];
    std::snprintf(buf, sizeof buf, u == 0 ? "%.0f %s" : "%.1f %s", v, unit[u]);
    return buf;
  }
  static std::string stamp(long long t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    if (!gmtime_r(&tt, &tm)) return "-";
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
  }
  static std::string permissions(unsigned int mode) {
    static const char* rwx[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
    std::string s;
    s += S_ISDIR(mode) ? 'd' : (S_ISLNK(mode) ? 'l' : '-');
    s += rwx[(mode >> 6) & 7];
    s += rwx[(mode >> 3) & 7];
    s += rwx[mode & 7];
    return s;
  }

  // How this terminal draws East Asian AMBIGUOUS glyphs. Not a preference and not a test hook:
  // it is a fact about the terminal the process cannot yet ask for, and a widget that guesses it
  // wrong cuts a two-cell glyph into one column.
  int ambiguous = 0;

  void prepare() {
    // RE-RESOLVE ONLY WHEN THE STORE MOVED. An edit made in the theme editor bumps the store's
    // version, and a frame that draws the old styles would make the editor look broken. A
    // version compare rather than a deep one, so an unchanged frame costs nothing.
    if (theme_store) {
      const unsigned long long v = rolltui_preset_store_version(theme_store);
      if (v != theme_seen) { theme_seen = v; sync_theme(); }
    }
    const RolltuiWidgetEnv env{static_cast<unsigned char>(ambiguous), now_ms};
    rolltui_context_set_env(ctx, &env);
    if (Browser* b = browser()) b->now_ms = now_ms;
    rolltui_context_set_bindings(ctx, bindings);
    rolltui_windows_sync(windows, stack);
    rolltui_windows_autosize(windows, stack, area());
    rolltui_windows_layout(windows, stack, area());
    note.clear();
    if (rolltui_windows_report_count(windows) != 0) {
      RolltuiStr s{};
      rolltui_windows_report_summary(windows, &s);
      note.assign(s.c_str(), s.size());
      rolltui_str_free(&s);
    }
  }

  // Once after each batch of events: did the browser end the session? An accept takes the
  // selected entry's path — or the column's own directory when the column is empty, which is
  // what the eye is on when there is nothing to select.
  void settle() {
    Browser* b = browser();
    if (!b) return;
    if (b->accepted) { chosen = b->selected_path(); exit_code = 0; quit = true; }
    else if (b->cancelled) quit = true;
  }

  // What the process leaves behind on stdout: the chosen path and a newline, or nothing at all.
  // Called after the terminal is restored, so the line lands on a normal screen.
  int finish() const {
    if (exit_code == 0) { std::fwrite(chosen.data(), 1, chosen.size(), stdout); std::fputc('\n', stdout); }
    return exit_code;
  }

  void toggle_popup(const char* id) {
    const std::size_t len = std::strlen(id);
    if (rolltui_window_stack_depth(stack) > 1) { rolltui_window_stack_pop(stack); return; }
    rolltui_window_stack_push_popup(stack, layout, id, len);
  }

  void run_action(const std::string& action) {
    Browser* b = browser();
    if (action == "app.quit") quit = true;
    else if (action == "app.jump") {
      // WALL 4 (phase file): `rolltui_window_stack_focus` takes a window ID, so an app that
      // wants to put the cursor in its own input must NAME a window the layout owns. roll does
      // the same for its find bar. There is no focus-by-CONTENT, which is what a host actually
      // knows — it bound `input:path`, it did not choose the id.
      rolltui_window_stack_focus(stack, "where", 5);
      hint = "type a path";
    }
    // A panel this screen declares is the library's to open — see
    // `rolltui_window_stack_action_popup`. `details`, `help`, `theme` and `keys` are four lines
    // this file does not have.
    else if (rolltui_window_stack_action_popup(stack, layout, action.data(), action.size())) {}
    else if (action == "app.hidden") { opt.hidden = !opt.hidden; if (b) b->reload(); hint = opt.hidden ? "dotfiles shown" : "dotfiles hidden"; }
    else if (action == "app.sort") {
      opt.sort = opt.sort == Sort::Name ? Sort::Size : opt.sort == Sort::Size ? Sort::Modified : Sort::Name;
      if (b) b->reload();
      hint = std::string("sorted by ") + (opt.sort == Sort::Name ? "name" : opt.sort == Sort::Size ? "size" : "modified");
    }
  }

  void handle(const RolltuiEvent& e) {
    // The app's OWN scope first, so a global chord works wherever the focus is — roll's rule.
    if (e.kind == ROLLTUI_EVENT_KEY) {
      std::size_t len = 0;
      if (const char* a = rolltui_bindings_action_for(bindings, &e.key, "app", 3, &len)) {
        if (len != 0) { run_action(std::string(a, len)); return; }
      }
    }
    RolltuiStr window{};
    const unsigned char kind =
        rolltui_window_stack_route(stack, &e, area(), bindings, rolltui_stack_default_actions(), &window);
    const std::string target(window.c_str(), window.size());
    rolltui_str_free(&window);
    if (kind != ROLLTUI_ROUTE_DELIVER) return;
    rolltui_windows_handle(windows, target.data(), target.size(), &e);
  }

  static void draw_slot(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
    App& a = *static_cast<App*>(ctx);
    rolltui_windows_draw(a.windows, rn, f, a.styles, rolltui_windows_default_roles());
  }

  void render_into(RolltuiFrame* f) {
    prepare();
    rolltui_window_stack_compose(stack, f, area(), styles, rolltui_layout_default_roles(), draw_slot, this, 0,
                                 compose_scratch);
    if (h <= 1) return;
    rolltui_frame_fill(f, draw_scratch, RolltuiRect{0, h - 1, w, 1}, style(ROLLTUI_ROLE_PANEL_BACKGROUND), nullptr, 0);
    // NAMED FACTS, DRAWN AS FACTS: the names muted and the answers bright, the same two roles
    // the columns above use. `status_rows` is reset and refilled rather than rebuilt, so a
    // frame that says nothing new allocates nothing to say it.
    Browser* b = browser();
    char num[64];
    status_rows.reset();
    rolltui_rows_add(&status_rows, "", 0, root.data(), root.size());
    // A REPORT OUTRANKS EVERY FACT BELOW IT: the line is truncated from the right, so anything
    // that must be read goes before anything that is merely useful.
    if (!note.empty()) rolltui_rows_add(&status_rows, "", 0, note.data(), note.size());
    // A DATA failure said the way the person who caused it will read it. The window report above
    // is the app author's channel and names a window and a content string; someone who mistyped a
    // path needs the path back, not the plumbing that carried it.
    if (b && !b->cols.empty() && !b->cols[0].error.empty())
      rolltui_rows_add(&status_rows, "", 0, b->cols[0].error.data(), b->cols[0].error.size());
    if (!hint.empty()) rolltui_rows_add(&status_rows, "", 0, hint.data(), hint.size());
    if (b) {
      const Column* c = b->focused();
      std::snprintf(num, sizeof num, "%zu", c ? c->entries.n : 0);
      status_rows.add("entries", num);
      std::snprintf(num, sizeof num, "%zu/%zu", b->focus_col + 1, b->cols.size());
      status_rows.add("column", num);
      status_rows.add("sort", opt.sort == Sort::Name ? "name" : opt.sort == Sort::Size ? "size" : "modified");
      if (opt.hidden) rolltui_rows_add(&status_rows, "", 0, "+dotfiles", 9);
    }
    rolltui_frame_put_fields(f, draw_scratch, 1, h - 1, &status_rows, style(ROLLTUI_ROLE_LABEL),
                             style(ROLLTUI_ROLE_VALUE), w - 1, 0);
  }
};

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// The app's own files, generated at build time from `examples/presets/` by
// cmake/embed_presets.cmake. Compiled in rather than written here, so the screen's words live in
// the JSON a user's preset directory shadows and in no hand-written source.
extern "C" {
extern const RolltuiEmbeddedFile dirktui_kAppFiles[];
extern const size_t dirktui_kAppFileCount;
}

// Where a person's own presets live, the same three rungs `rolltui_app_file` walks for an app's
// own files: an explicit configuration directory, then the XDG one, then the home default.
std::string user_presets_dir() {
  if (const char* d = std::getenv("ROLL_CONFIG_DIR"); d && *d) return std::string(d) + "/rolltui";
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return std::string(x) + "/roll/rolltui";
  const char* home = std::getenv("HOME");
  return std::string(home && *home ? home : ".") + "/.config/roll/rolltui";
}

RolltuiLayout* load_layout_text(RolltuiContext* ctx, const std::string& text, RolltuiLayoutReport* rep) {
  std::size_t defaults_n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx, &defaults_n);
  return rolltui_load_layout_text(text.data(), text.size(), defaults, defaults_n,
                                  rolltui_layout_default_hooks(), rep);
}

[[maybe_unused]] bool parse_size(const std::string& s, int& w, int& h) {
  const std::size_t x = s.find('x');
  if (x == std::string::npos) return false;
  w = std::atoi(s.substr(0, x).c_str());
  h = std::atoi(s.substr(x + 1).c_str());
  return w > 0 && h > 0;
}

// The script vocabulary is the shared self-test header's, not this file's. This app used to
// carry a partial re-implementation of it.

int usage() {
  std::fprintf(stderr,
               "usage: dirktui [PATH] [--ambiguous-wide]   browse from PATH (default: the current directory)\n"
               "                                           Enter prints the selection on stdout and exits 0;\n"
               "                                           Esc prints nothing and exits 1\n"
               "       dirktui init zsh|bash|fish          the shell side: a `dirk` function and Right Arrow\n"
               "                                           zsh:  eval \"$(dirktui init zsh)\"    (bash likewise)\n"
               "                                           fish: dirktui init fish | source\n"
#ifdef ROLLTUI_SELFTEST
               "       [--presets DIR] [--layout NAME|FILE] [--theme NAME]\n"
               "       [--frame WxH] [--keys \"Down Right CtrlD\"]\n"
#endif
               );
  return 2;
}

// THE SHELL SIDE, printed by `dirktui init <shell>` so it is versioned with the binary it drives
// (zoxide, atuin and fzf all ship their shell code this way). The three scripts say the same
// thing in three dialects; what they say, so a reader of the C++ knows the other half:
//   * A CHOSEN PATH MEANS ONE COMMAND LINE: a folder is `cd`'d into; an executable file means
//     `cd` to its folder; any other file means `cd` to its folder AND open it ($DIRK_OPEN,
//     defaulting to xdg-open where that exists and `open` otherwise). The line is composed once
//     (`_dirk_command`) and is what runs, what goes into history, and what a person sees.
//   * `dirk [PATH]` at a prompt browses, then runs that line; a chosen file is printed first so
//     the eye knows what it is now beside. `dirk init …` passes through to the binary.
//   * Right Arrow with the cursor at the END of the line opens the browser. On an empty line the
//     composed command runs — in zsh through accept-line (fzf's alt-c), in bash through
//     `history -s` + eval, in fish directly — so it is in history where the shell allows. On a
//     line with text the choice is inserted at the cursor, quoted, starting from and replacing
//     the last word when that word names a folder (`ls src/<Right>`). Fish keeps its own Right
//     on a non-empty line, because a pending autosuggestion cannot be asked about there.
//   * Right Arrow anywhere else, or with an autosuggestion showing, is what it always was.
constexpr const char* kZshInit = R"zsh(# dirk — zsh integration for dirktui. In ~/.zshrc:   eval "$(dirktui init zsh)"
: "${DIRK_OPEN:=$( (( $+commands[xdg-open] )) && print xdg-open || print open )}"

# A chosen path as the ONE command line that acts on it: the line that runs and the line history keeps.
_dirk_command() {
  local out="$1"
  if [[ -d "$out" ]]; then
    print -r -- "builtin cd -- ${(q)out}"
  elif [[ -x "$out" ]]; then
    print -r -- "builtin cd -- ${(q)out:h}"
  else
    print -r -- "builtin cd -- ${(q)out:h} && ${DIRK_OPEN} ${(q)out}"
  fi
}

dirk() {
  if [[ "$1" == init ]]; then command dirktui "$@"; return $?; fi
  local out
  out="$(command dirktui "$@")" || return $?
  [[ -n "$out" ]] || return 1
  [[ -d "$out" ]] || print -r -- "$out"
  eval "$(_dirk_command "$out")"
}

_dirk_widget() {
  emulate -L zsh
  local start='' word='' out
  if [[ -n "$LBUFFER" ]]; then
    word="${LBUFFER##* }"
    local probe="$word"
    [[ "$probe" == '~' || "$probe" == '~/'* ]] && probe="$HOME${probe#\~}"
    [[ -n "$probe" && -d "$probe" ]] && start="$probe"
  fi
  out="$(command dirktui ${start:+"$start"} < /dev/tty)"
  if [[ -z "$out" ]]; then
    zle redisplay
    return 0
  fi
  if [[ -z "$BUFFER" ]]; then
    zle push-line
    BUFFER="$(_dirk_command "$out")"
    zle accept-line
    local ret=$?
    zle reset-prompt
    return $ret
  fi
  [[ -n "$start" ]] && LBUFFER="${LBUFFER%"$word"}"
  LBUFFER+="${(q)out}"
  zle reset-prompt
}
zle -N _dirk_widget

_dirk_forward_char() {
  if (( CURSOR < ${#BUFFER} )) || [[ -n "$POSTDISPLAY" ]]; then
    zle forward-char
  else
    zle _dirk_widget
  fi
}
zle -N _dirk_forward_char
bindkey -M emacs '^[[C' _dirk_forward_char
bindkey -M emacs '^[OC' _dirk_forward_char
bindkey -M viins '^[[C' _dirk_forward_char
bindkey -M viins '^[OC' _dirk_forward_char
)zsh";

constexpr const char* kBashInit = R"bash(# dirk — bash integration for dirktui. In ~/.bashrc:   eval "$(dirktui init bash)"
: "${DIRK_OPEN:=$(command -v xdg-open >/dev/null 2>&1 && echo xdg-open || echo open)}"

# A chosen path as the ONE command line that acts on it: the line that runs and the line history keeps.
_dirk_command() {
  local out="$1" dir
  dir="$(dirname -- "$out")"
  if [[ -d "$out" ]]; then
    printf 'builtin cd -- %q' "$out"
  elif [[ -x "$out" ]]; then
    printf 'builtin cd -- %q' "$dir"
  else
    printf 'builtin cd -- %q && %s %q' "$dir" "$DIRK_OPEN" "$out"
  fi
}

dirk() {
  if [[ "$1" == init ]]; then command dirktui "$@"; return $?; fi
  local out
  out="$(command dirktui "$@")" || return $?
  [[ -n "$out" ]] || return 1
  [[ -d "$out" ]] || printf '%s\n' "$out"
  eval "$(_dirk_command "$out")"
}

# READLINE_POINT is a BYTE offset into READLINE_LINE; lengths compared to it are measured in bytes.
_dirk_bytes() { local LC_ALL=C; printf '%s' "${#1}"; }

_dirk_forward_char() {
  local len
  len="$(_dirk_bytes "$READLINE_LINE")"
  if (( READLINE_POINT < len )); then
    # One CHARACTER forward, however many bytes it is.
    local head next
    head="$(LC_ALL=C; printf '%s' "${READLINE_LINE:0:READLINE_POINT}")"
    next="${READLINE_LINE:${#head}:1}"
    READLINE_POINT=$(( READLINE_POINT + $(_dirk_bytes "$next") ))
    return 0
  fi
  local start='' word='' out
  if [[ -n "$READLINE_LINE" ]]; then
    word="${READLINE_LINE##* }"
    local probe="$word"
    [[ "$probe" == '~' || "$probe" == '~/'* ]] && probe="$HOME${probe#\~}"
    [[ -n "$probe" && -d "$probe" ]] && start="$probe"
  fi
  out="$(command dirktui ${start:+"$start"} < /dev/tty)" || return 0
  [[ -n "$out" ]] || return 0
  if [[ -z "$READLINE_LINE" ]]; then
    local cmd
    cmd="$(_dirk_command "$out")"
    history -s "$cmd"
    eval "$cmd"
    return 0
  fi
  [[ -n "$start" ]] && READLINE_LINE="${READLINE_LINE%"$word"}"
  READLINE_LINE+="$(printf '%q' "$out")"
  READLINE_POINT="$(_dirk_bytes "$READLINE_LINE")"
}
bind -m emacs-standard -x '"\e[C": _dirk_forward_char'
bind -m emacs-standard -x '"\eOC": _dirk_forward_char'
bind -m vi-insert -x '"\e[C": _dirk_forward_char'
bind -m vi-insert -x '"\eOC": _dirk_forward_char'
)bash";

constexpr const char* kFishInit = R"fish(# dirk — fish integration for dirktui. In config.fish:   dirktui init fish | source
if not set -q DIRK_OPEN
    if type -q xdg-open
        set -g DIRK_OPEN xdg-open
    else
        set -g DIRK_OPEN open
    end
end

# What a chosen path means: a folder is entered; a file lands in its folder, is printed, and is
# opened unless it is executable.
function _dirk_go --argument-names out
    if test -d "$out"
        builtin cd -- "$out"
        return
    end
    printf '%s\n' "$out"
    builtin cd -- (dirname -- "$out"); or return
    test -x "$out"; or $DIRK_OPEN "$out"
end

function dirk
    if test "$argv[1]" = init
        command dirktui $argv
        return
    end
    set -l out (command dirktui $argv); or return
    test -n "$out"; or return 1
    _dirk_go "$out"
end

# Right Arrow opens the browser only on an EMPTY line. With text on the line fish may be showing
# an autosuggestion, which cannot be asked about here, so Right stays fish's own.
function _dirk_forward_char
    if test -n (commandline)
        commandline -f forward-char
        return
    end
    set -l out (command dirktui </dev/tty)
    commandline -f repaint
    test -n "$out"; or return
    _dirk_go "$out"
    commandline -f repaint
end
bind \e\[C _dirk_forward_char
bind \eOC _dirk_forward_char
)fish";

// `dirktui init <shell>`: the integration for that shell on stdout. A name this binary has no
// script for is refused with the list it has, never answered with another shell's.
int init_command(int argc, char** argv) {
  const std::string shell = argc == 3 ? argv[2] : "";
  const char* script = shell == "zsh" ? kZshInit : shell == "bash" ? kBashInit : shell == "fish" ? kFishInit : nullptr;
  if (!script) {
    std::fprintf(stderr, "usage: dirktui init zsh|bash|fish\n");
    return 2;
  }
  std::fputs(script, stdout);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // A subcommand is the FIRST word and nothing else: a directory literally called `init` is
  // still reachable as `dirktui ./init`.
  if (argc >= 2 && std::string(argv[1]) == "init") return init_command(argc, argv);
  std::string start;
  bool ambiguous = false;
  [[maybe_unused]] std::string presets_dir, layout_arg, theme_arg = "default-dark";
  [[maybe_unused]] std::string frame_spec, keys_spec;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    [[maybe_unused]] auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    // WHAT A FLAG ON THIS COMMAND LINE MAY BE, and the four are not close:
    //   1. A SELF-TEST HOOK — compiled in only for `dirk-selftest`, which is this same
    //      source built again WITH them. The shipped binary does not contain them, so the binary
    //      that gets verified is not the one that ships.
    //   2. A REAL FEATURE RUN HEADLESSLY — a shipped capability reached without a terminal. Stays.
    //   3. A TERMINAL FACT — something true of the terminal the process cannot yet ask for.
    //      `--ambiguous-wide` is the last one; it becomes an auto-detected setting.
    //   4. CONFIGURATION — a theme, a layout, a bindings file, a preset directory, a mode, a
    //      depth. **These may never come back.** Each names something the preset system already
    //      holds, autosaves and offers a UI for, and a flag beside it is a second configuration
    //      system with neither discoverability nor persistence, competing with the one that has
    //      both — and winning by accident, because a flag is what a person finds first.
    // `rolltui-product-flags-test` holds all three products to this.
    if (a == "--ambiguous-wide") { ambiguous = true; continue; }  // a terminal fact, not a hook
#ifdef ROLLTUI_SELFTEST
    if (a == "--presets") presets_dir = next();
    else if (a == "--layout") layout_arg = next();
    else if (a == "--theme") theme_arg = next();
    else if (a == "--frame") frame_spec = next();
    else if (a == "--keys") keys_spec = next();
    else
#endif
    if (!a.empty() && a[0] != '-' && start.empty()) start = a;
    else return usage();
  }

  App app;
  app.ambiguous = ambiguous ? 1 : 0;
  app.set_theme(theme_arg.c_str());
  if (!app.effects) app.set_theme("default-dark");
  rolltui_context_set_dir(app.ctx, presets_dir.data(), presets_dir.size());

  // THE STORES, and the two lines that make the library's editors this app's. A kind is the
  // library's; what it edits is whatever store the host hands over, so an app with no store gets
  // an editor with nowhere to commit. Opened on the same directory the context resolves presets
  // through, so what the editors write is what the next start reads.
  {
    // The STORES read a person's own directory whether or not one was named on the command line —
    // an explicit `--presets` points the SCREEN somewhere, and where a person's presets live is a
    // separate question with its own answer.
    const std::string store_dir = presets_dir.empty() ? user_presets_dir() : presets_dir;
    RolltuiPresetDomain* td = rolltui_preset_domain_theme(app.ctx);
    RolltuiPresetDomain* bd = rolltui_preset_domain_bindings(app.ctx);
    RolltuiThemePresetReport trep{};
    RolltuiBindingsPresetReport brep{};
    app.theme_store = rolltui_preset_store_new(td, store_dir.data(), store_dir.size(), 0, "", 0);
    app.keys_store = rolltui_preset_store_new(bd, store_dir.data(), store_dir.size(), 0, "", 0);
    // A report is REQUIRED, not optional: a store that cannot say what it did on start would make
    // a missing preset directory look like a successful one.
    rolltui_preset_store_start(app.theme_store, &trep);
    rolltui_preset_store_start(app.keys_store, &brep);
    rolltui_theme_preset_report_release(&trep);
    rolltui_bindings_preset_report_release(&brep);
    rolltui_windows_set_theme_store(app.windows, "theme", 5, app.theme_store, /*persist=*/1);
    rolltui_windows_set_bindings_store(app.windows, "keys", 4, app.keys_store, /*persist=*/1);
    app.sync_theme();
  }

  {
    char cwd[4096];
    app.root = start.empty() ? (getcwd(cwd, sizeof cwd) ? cwd : ".") : start;
  }

  RolltuiLayoutReport rep{};
  RolltuiLayout* loaded = nullptr;  // OWNED
  bool have = false;
  RolltuiStr layout_tried{};
  {
    // An explicit --layout or --presets is a direct instruction and is taken as given. With
    // neither, the app asks the library where its OWN default lives, which is what lets it run bare.
    const bool named = !layout_arg.empty() || !presets_dir.empty();
    if (named) {
      const std::string name = layout_arg.empty() ? std::string("dirktui") : layout_arg;
      const bool path = name.find('/') != std::string::npos || name.find(".json") != std::string::npos;
      const std::string file = path ? name : presets_dir + "/layouts/" + name + ".json";
      bool ok = false;
      const std::string text = read_file(file, ok);
      if (ok) { loaded = load_layout_text(app.ctx, text, &rep); have = loaded != nullptr; }
      if (!ok) { rolltui_str_append(&layout_tried, "  missing ", 10);
                 rolltui_str_append(&layout_tried, file.data(), file.size()); }
    } else {
      RolltuiStr text{};
      if (rolltui_app_file(argv[0], "dirktui", "layout",
                           dirktui_kAppFiles, dirktui_kAppFileCount, &text, &layout_tried)) {
        loaded = load_layout_text(app.ctx, std::string(text.p ? text.p : "", text.n), &rep);
        have = loaded != nullptr;
      }
      rolltui_str_free(&text);
    }
  }
  if (!have) {
    // NAME WHAT WAS WANTED AND EVERY PLACE IT WAS SOUGHT. "no layout ()" was this message, and
    // an empty parenthesis is the standard this library enforces on everyone else, failed here.
    std::fprintf(stderr, "dirktui: cannot load its layout%s%s\n",
                 rep.error.empty() ? "" : ": ", rep.error.c_str());
    if (layout_tried.n) std::fprintf(stderr, "tried:\n%.*s\n", (int)layout_tried.n, layout_tried.p);
    rolltui_str_free(&layout_tried);
    rolltui_layout_free(loaded);
    rolltui_layout_report_release(&rep);
    return 1;
  }
  rolltui_str_free(&layout_tried);
  rolltui_layout_free(app.layout);
  app.layout = loaded;  // TAKES OWNERSHIP
  app.mount();
  rolltui_layout_report_release(&rep);

  // THE BINDINGS FILE, AND IT LOADS AFTER `mount()` ON PURPOSE — wall 5 in the phase file. A
  // row naming an action the table has not been told about is an `unknown_actions` entry and
  // its chord is dropped, and `rolltui_bindings_declare` (inside `mount`) is what tells it. The
  // first draft loaded the file first and every `app.*` and `browser.*` chord silently vanished:
  // the app ran, the keys did nothing, and no report said why, because the report belonged to a
  // load that had already been released.
  {
    bool ok = false;
    std::string text;
    if (!presets_dir.empty()) text = read_file(presets_dir + "/bindings/default.json", ok);
    else {
      RolltuiStr t{};
      ok = rolltui_app_file(argv[0], "dirktui", "bindings",
                            dirktui_kAppFiles, dirktui_kAppFileCount, &t, nullptr) != 0;
      if (ok) text.assign(t.p ? t.p : "", t.n);
      rolltui_str_free(&t);
    }
    if (ok) {
      RolltuiBindingsReport brep{};
      rolltui_bindings_load_json(app.bindings, text.data(), text.size(), ROLLTUI_PROTOCOL_LEGACY,
                                 rolltui_bindings_library_scope, nullptr, nullptr, nullptr, &brep);
      // EVERY category, not just the one that bit first (wall 5): a chord the library's shipped
      // table already owns is a CONFLICT, and a conflict nobody prints is an action that
      // silently does nothing. `ctrl+h` and `ctrl+l` were exactly that — the shipped input
      // bindings hold them — and the app looked broken rather than configured.
      //
      // ONE CALL, not six loops over the report's arrays. `rolltui_bindings_report_summary` is
      // public precisely so a host that loads its own bindings file does not hand-write them —
      // an INTERNAL summary makes every such host write the wrapper the library already has.
      RolltuiStr why{};
      rolltui_bindings_report_summary(&brep, &why);
      if (why.size() != 0)
        std::fprintf(stderr, "dirktui: bindings/default.json: %s\n", why.c_str());
      rolltui_str_free(&why);
      rolltui_bindings_report_release(&brep);
    }
  }

  // ---- END OF INIT: what this screen NAMES that this app does not PROVIDE --------
  // The kinds are registered, the sources are bound and the bindings are loaded, so this is the
  // one moment the question is answerable. It REPORTS: a gap is a to-do for whoever builds this
  // app, never a reason to refuse the screen — so nothing below branches on it. A layout naming
  // a kind nobody has written yet is a design that has run ahead of the code, which is allowed.
  {
    RolltuiGapReport gaps{};
    rolltui_gaps_collect(app.windows, app.layout, app.bindings, &gaps);
    if (!rolltui_gap_report_clean(&gaps)) {
      RolltuiStr say{};
      rolltui_gap_report_summary(&gaps, &say);
      std::fprintf(stderr, "dirktui: %s\n", say.c_str());
      rolltui_str_free(&say);
    }
    rolltui_gap_report_release(&gaps);
  }

#ifdef ROLLTUI_SELFTEST
  if (!frame_spec.empty()) {
    if (!parse_size(frame_spec, app.w, app.h)) return usage();
    app.prepare();
    if (!keys_spec.empty()) {
      for (const rolltui_selftest::Step& st : rolltui_selftest::scripted_keys(keys_spec, app.w, app.h))
        if (!st.tick) app.handle(st.ev);
      app.settle();
      // A script that accepts or cancels gets the PRODUCT's answer — the path or nothing, with
      // its exit status — and no frame, so the headless run and the real one leave the same bytes.
      if (app.quit) return app.finish();
      app.prepare();
    }
    RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    std::fwrite(text.c_str(), 1, text.size(), stdout);
    rolltui_str_free(&text);
    rolltui_swap_free(swap);
    return 0;
  }
#endif

  // A PERSON AT A KEYBOARD IS THE PRECONDITION, checked on stdin BEFORE the terminal is touched.
  // dirk takes its keys from whoever is typing, and a stdin that is a pipe means there is nobody —
  // a script, a test harness, `yes | dirk`. Refusing here is what keeps a headless run from
  // opening the controlling terminal and sitting in raw mode waiting for a key nobody will press.
  // The shell side redirects `< /dev/tty` for the same reason fzf's widget does.
  if (!isatty(STDIN_FILENO)) {
    std::fprintf(stderr, "dirktui: stdin is not a terminal, and dirk takes its keys from the keyboard\n");
    return 2;
  }
  // THE SCREEN IS /dev/tty, ALWAYS — not stdout when stdout happens to be a terminal. One
  // behaviour, whether run bare or inside `$(dirk)`, and stdout carries nothing but the answer.
  // No /dev/tty means nowhere to draw: said by name, exit 2, never a hang reading a pipe.
  const int tty = open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (tty < 0) {
    std::fprintf(stderr, "dirktui: cannot open /dev/tty (%s): no terminal to draw on\n", std::strerror(errno));
    return 2;
  }
  RolltuiTerminalOptions opts{};
  RolltuiTerminal* term = rolltui_terminal_new(tty, tty, opts);
  if (!rolltui_terminal_is_tty(term)) {
    rolltui_terminal_free(term);
    close(tty);
    std::fprintf(stderr, "dirktui: /dev/tty is not a terminal\n");
    return 2;
  }
  app.w = rolltui_terminal_width(term);
  app.h = rolltui_terminal_height(term);
  RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
  RolltuiStr out{};
  struct Pending {
    std::vector<RolltuiEvent> events;
    std::vector<std::string> texts;
    std::vector<std::size_t> text_of;
    int w = 0, h = 0;
    bool resized = false;
  } pending;
  const std::size_t kNone = static_cast<std::size_t>(-1);
  while (!app.quit) {
    app.now_ms = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    const int timeout = app.poll_timeout_ms(250);
    out.clear();
    rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, &out);
    rolltui_terminal_write(term, out.c_str(), out.size());
    pending.events.clear();
    pending.texts.clear();
    pending.text_of.clear();
    pending.resized = false;
    pending.w = app.w;
    pending.h = app.h;
    rolltui_terminal_poll(
        term, timeout,
        [](void* ctx, const RolltuiTermEvent* e) {
          Pending& p = *static_cast<Pending*>(ctx);
          if (e->kind == ROLLTUI_TERM_EVENT_RESIZE) { p.w = e->w; p.h = e->h; p.resized = true; return; }
          RolltuiEvent ev{};
          ev.kind = e->kind;
          ev.key = e->key;
          ev.mouse = e->mouse;
          std::size_t slot = static_cast<std::size_t>(-1);
          if (e->text) { slot = p.texts.size(); p.texts.emplace_back(e->text, e->text_len); ev.text_len = e->text_len; }
          p.text_of.push_back(slot);
          p.events.push_back(ev);
        },
        &pending);
    for (std::size_t i = 0; i < pending.events.size(); ++i)
      if (pending.text_of[i] != kNone) pending.events[i].text = pending.texts[pending.text_of[i]].data();
    for (const RolltuiEvent& e : pending.events) app.handle(e);
    app.settle();
    if (pending.resized) { app.w = pending.w; app.h = pending.h; }
  }
  rolltui_str_free(&out);
  rolltui_swap_free(swap);
  rolltui_terminal_free(term);  // restores the screen BEFORE the answer is written
  close(tty);
  return app.finish();
}
