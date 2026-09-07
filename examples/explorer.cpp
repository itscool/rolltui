//
// rolltui/examples/explorer.cpp — `rolltui-explorer`, the library's FOURTH consumer
// and the first host whose widget has INTERNAL STRUCTURE the library does
// not already model.
//
// It is a READ-ONLY file-system browser in the shape macOS calls column view (Miller
// columns): side-by-side lists, the selection in a column filling the one to its right, a
// horizontal scroll when the path is deeper than the window, and a vertical scroll per column.
// It never writes, renames, moves or deletes anything — `opendir`, `readdir` and `lstat` are
// the whole of its contact with the file system.
//
// ============================================================================================
// WHY IT EXISTS, AND WHY IT IS NOT A DEMO
//
// roll and the studio are both a document with a prompt under it; `rolltui-paint` is a canvas
// that owns its own pixels and fills ONE rectangle. So every property the library grew could
// still have been a property of those two shapes, and the widget plugin's nine slots had never
// been asked to carry a widget with children, two scroll axes, a selection that propagates
// sideways and a width that depends on its contents. This app asks them.
//
// Its value is therefore the WALL LOG in the plan, not the screenshots: every place
// the public header could not do something, and what was done instead. It includes
// `rolltui/rolltui.h` and NOTHING else of the library's — it is a consumer like paint, not
// like the studio, and `public_header_test` asserts that rather than trusting it.
//
// ============================================================================================
// WHAT IS THE APP'S AND WHAT IS THE SCREEN'S
//
// The screen is FILES (`examples/presets/`): the layout names the windows and DECLARES every
// action, the bindings file says which chord runs each one, and the menu is a file too. No
// string below names a window id, and `explorer_test`'s grep asserts it — the same control
// `files_only_test` runs for the studio.
//
// The BROWSER OWNS ITS MODEL, deliberately. It could have been a `rows:` binding refilled by
// the host, and that would have proved nothing: the point is a plugin with data and structure
// of its own, which is the case a two-callback adapter cannot serve.
//
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/selftest/script.hpp"

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

struct Entry {
  std::string name;
  bool is_dir = false;
  bool unreadable = false;   // lstat failed: shown, never guessed about
  long long size = 0;
  long long mtime = 0;
  unsigned int mode = 0;
};

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
  std::vector<Entry> entries;
  std::size_t sel = 0;
  std::size_t top = 0;   // the first visible row: this column's own vertical scroll
  int width = 18;        // derived from the content, clamped
  std::size_t hidden_n = 0;
  std::string error;     // opendir failed: a NOTE, never a crash
};

bool read_dir(const std::string& path, const Options& opt, Column& out) {
  out.entries.clear();
  out.hidden_n = 0;
  out.error.clear();
  DIR* d = opendir(path.c_str());
  if (!d) {
    out.error = "cannot open " + path;
    return false;
  }
  while (const dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    if (!name.empty() && name[0] == '.' && !opt.hidden) {
      ++out.hidden_n;
      continue;
    }
    Entry en;
    en.name = name;
    struct stat st {};
    const std::string full = path == "/" ? "/" + name : path + "/" + name;
    if (lstat(full.c_str(), &st) == 0) {
      en.is_dir = S_ISDIR(st.st_mode);
      en.size = static_cast<long long>(st.st_size);
      en.mtime = static_cast<long long>(st.st_mtime);
      en.mode = static_cast<unsigned int>(st.st_mode);
    } else {
      en.unreadable = true;
    }
    out.entries.push_back(std::move(en));
  }
  closedir(d);
  // Directories first, then the chosen order — and NAME always breaks a tie, so a frame is a
  // pure function of the tree rather than of readdir's order.
  const Sort s = opt.sort;
  std::sort(out.entries.begin(), out.entries.end(), [s](const Entry& a, const Entry& b) {
    if (a.is_dir != b.is_dir) return a.is_dir;
    if (s == Sort::Size && a.size != b.size) return a.size > b.size;
    if (s == Sort::Modified && a.mtime != b.mtime) return a.mtime > b.mtime;
    return a.name < b.name;
  });
  return true;
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
  std::size_t first_col = 0;  // the leftmost visible column: the horizontal scroll
  std::string root;

  const Column* focused() const { return focus_col < cols.size() ? &cols[focus_col] : nullptr; }
  Column* focused() { return focus_col < cols.size() ? &cols[focus_col] : nullptr; }

  const Entry* selected() const {
    const Column* c = focused();
    return c && c->sel < c->entries.size() ? &c->entries[c->sel] : nullptr;
  }
  std::string selected_path() const {
    const Column* c = focused();
    if (!c || c->sel >= c->entries.size()) return c ? c->dir : root;
    return c->dir == "/" ? "/" + c->entries[c->sel].name : c->dir + "/" + c->entries[c->sel].name;
  }

  void set_root(const std::string& path) {
    root = path;
    cols.clear();
    focus_col = 0;
    first_col = 0;
    Column c;
    c.dir = path;
    read_dir(path, *opt, c);
    measure_width(c);
    cols.push_back(std::move(c));
    open_selected();
  }

  void reload() {
    // Re-read every column in place, keeping the selection BY NAME rather than by index, so a
    // sort change or a dotfile toggle does not move the eye to a different file.
    std::vector<std::string> keep;
    for (const Column& c : cols) keep.push_back(c.sel < c.entries.size() ? c.entries[c.sel].name : std::string());
    for (std::size_t i = 0; i < cols.size(); ++i) {
      read_dir(cols[i].dir, *opt, cols[i]);
      measure_width(cols[i]);
      cols[i].sel = 0;
      for (std::size_t j = 0; j < cols[i].entries.size(); ++j)
        if (cols[i].entries[j].name == keep[i]) cols[i].sel = j;
      clamp_scroll(cols[i]);
    }
  }

  void measure_width(Column& c) {
    int longest = 0;
    for (const Entry& e : c.entries) longest = std::max(longest, measure.width(e.name) + (e.is_dir ? 2 : 0));
    c.width = std::min(28, std::max(12, longest + 2));
  }

  // The column to the right of the focused one exists exactly when a directory is selected.
  void open_selected() {
    cols.resize(focus_col + 1);
    const Entry* e = selected();
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
    if (c.entries.size() <= static_cast<std::size_t>(vis)) c.top = 0;
  }

  // Keep the focused column on screen: the horizontal scroll follows the eye, so a deep path
  // scrolls left exactly as a column view does.
  void clamp_columns() {
    if (focus_col < first_col) first_col = focus_col;
    for (;;) {
      int used = 0;
      std::size_t last = first_col;
      for (std::size_t i = first_col; i < cols.size(); ++i) {
        if (used + cols[i].width > inner.w && i > first_col) break;
        used += cols[i].width + 1;
        last = i;
      }
      if (focus_col <= last || first_col + 1 >= cols.size()) break;
      ++first_col;
    }
  }

  void move(int delta) {
    Column* c = focused();
    if (!c || c->entries.empty()) return;
    long long at = static_cast<long long>(c->sel) + delta;
    at = std::max<long long>(0, std::min<long long>(at, static_cast<long long>(c->entries.size()) - 1));
    c->sel = static_cast<std::size_t>(at);
    clamp_scroll(*c);
    open_selected();
  }
  void select(std::size_t i) {
    Column* c = focused();
    if (!c || i >= c->entries.size()) return;
    c->sel = i;
    clamp_scroll(*c);
    open_selected();
  }
  void into() {
    const Entry* e = selected();
    if (!e || !e->is_dir) return;
    open_selected();
    if (focus_col + 1 < cols.size()) {
      ++focus_col;
      clamp_columns();
    }
  }
  void out() {
    if (focus_col > 0) {
      --focus_col;
      cols.resize(focus_col + 2 <= cols.size() ? focus_col + 2 : cols.size());
      clamp_columns();
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
    for (std::size_t i = 0; i < c->entries.size(); ++i)
      if (c->entries[i].name == child) c->sel = i;
    clamp_scroll(*c);
    open_selected();
  }
};

void browser_destroy(void* ctx) {
  Browser* b = static_cast<Browser*>(ctx);
  rolltui_draw_scratch_free(b->draw_scratch);
  delete b;
}

int browser_problem(void* ctx, RolltuiStr* out) {
  const Browser* b = static_cast<const Browser*>(ctx);
  if (!b->cols.empty() && b->cols[0].error.empty()) return 0;
  const std::string why = b->cols.empty() ? "nothing is bound to '" + b->source + "'" : b->cols[0].error;
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
  b->clamp_columns();
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

  int x = r.x;
  for (std::size_t ci = b->first_col; ci < b->cols.size() && x < r.x + r.w; ++ci) {
    const Column& c = b->cols[ci];
    const int cw = std::min(c.width, r.x + r.w - x);
    if (cw <= 0) break;
    // The column's own head: the directory's last component, so a deep path stays readable.
    const std::size_t slash = c.dir.find_last_of('/');
    const std::string title = c.dir == "/" ? "/" : c.dir.substr(slash == std::string::npos ? 0 : slash + 1);
    rolltui_frame_put_text(f, b->draw_scratch, x, r.y, b->measure.fit(title, cw).data(),
                           b->measure.fit(title, cw).size(), head, cw, 0, 0);
    const int rows = b->rows_visible();
    for (int row = 0; row < rows; ++row) {
      const std::size_t i = c.top + static_cast<std::size_t>(row);
      const int y = r.y + 1 + row;
      if (y >= r.y + r.h) break;
      if (i >= c.entries.size()) break;
      const Entry& e = c.entries[i];
      const bool is_sel = i == c.sel;
      const bool is_focus_col = ci == b->focus_col;
      const RolltuiStyle st = is_sel ? (is_focus_col ? here : trail) : (e.is_dir ? text : (e.unreadable ? dim : text));
      if (is_sel) rolltui_frame_fill(f, b->draw_scratch, RolltuiRect{x, y, cw, 1}, st, nullptr, 0);
      // A directory is marked with a trailing chevron rather than a colour, so the shape
      // survives `mono` and a colour-blind reader alike.
      const std::string label = e.name + (e.is_dir ? " \xE2\x80\xBA" : "");
      const std::string cut = b->measure.fit(label, cw - 1);
      rolltui_frame_put_text(f, b->draw_scratch, x + 1, y, cut.data(), cut.size(), st, cw - 1, 0, 0);
    }
    if (c.entries.empty()) {
      const char* empty = "(empty)";
      rolltui_frame_put_text(f, b->draw_scratch, x + 1, r.y + 1, empty, std::strlen(empty), dim, cw - 1, 0, 0);
    }
    x += cw + 1;
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
    out->total = c->entries.size();
    return 1;
  }
  out->first = b->first_col;
  out->visible = 1;
  out->total = b->cols.size();
  return 1;
}

int browser_scroll_to(void* ctx, unsigned char axis, std::size_t first) {
  Browser* b = static_cast<Browser*>(ctx);
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  Column* c = b->focused();
  if (!c) return 0;
  const int vis = b->rows_visible();
  const std::size_t max_top = c->entries.size() > static_cast<std::size_t>(vis)
                                  ? c->entries.size() - static_cast<std::size_t>(vis)
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
    int x = b->inner.x;
    for (std::size_t ci = b->first_col; ci < b->cols.size(); ++ci) {
      const int cw = b->cols[ci].width;
      if (e->mouse.x >= x && e->mouse.x < x + cw) {
        b->focus_col = ci;
        b->cols.resize(ci + 1);
        const int row = e->mouse.y - b->inner.y - 1;
        if (row >= 0) b->select(b->cols[ci].top + static_cast<std::size_t>(row));
        else b->open_selected();
        b->clamp_columns();
        return 1;
      }
      x += cw + 1;
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
  else if (action == "browser.last") { const Column* c = b->focused(); if (c && !c->entries.empty()) b->select(c->entries.size() - 1); }
  else if (action == "browser.into") b->into();
  else if (action == "browser.out") b->out();
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

  App() {
    layout = rolltui_layout_new();
    rolltui_context_set_library_defaults(ctx);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() {
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
    const Entry* e = b ? b->selected() : nullptr;
    if (!e) {
      rolltui_rows_add(out, "entry", 5, "(none)", 6);
      return;
    }
    const std::string path = b->selected_path();
    rolltui_rows_add(out, "name", 4, e->name.data(), e->name.size());
    rolltui_rows_add(out, "folder", 6, path.data(), path.size());
    const char* kind = e->unreadable ? "unreadable" : e->is_dir ? "directory" : "file";
    rolltui_rows_add(out, "kind", 4, kind, std::strlen(kind));
    const std::string size = e->is_dir ? std::string("-") : human_size(e->size);
    rolltui_rows_add(out, "size", 4, size.data(), size.size());
    const std::string when = stamp(e->mtime);
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

  void prepare() {
    const RolltuiWidgetEnv env{0, 0};
    rolltui_context_set_env(ctx, &env);
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
    else if (action == "app.details") toggle_popup("details");
    else if (action == "app.help") toggle_popup("help");
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
    if (!hint.empty()) rolltui_rows_add(&status_rows, "", 0, hint.data(), hint.size());
    if (b) {
      const Column* c = b->focused();
      std::snprintf(num, sizeof num, "%zu", c ? c->entries.size() : 0);
      status_rows.add("entries", num);
      std::snprintf(num, sizeof num, "%d/%zu", b->focus_col + 1, b->cols.size());
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
extern const RolltuiEmbeddedFile explorer_kAppFiles[];
extern const size_t explorer_kAppFileCount;
}

RolltuiLayout* load_layout_text(RolltuiContext* ctx, const std::string& text, RolltuiLayoutReport* rep) {
  std::size_t defaults_n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx, &defaults_n);
  return rolltui_load_layout_text(text.data(), text.size(), defaults, defaults_n,
                                  rolltui_layout_default_hooks(), rep);
}

bool parse_size(const std::string& s, int& w, int& h) {
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
               "usage: rolltui-explorer [PATH] [--presets DIR] [--layout NAME|FILE] [--theme NAME]\n"
               "                        [--frame WxH] [--keys \"Down Right CtrlD\"]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string presets_dir, layout_arg, theme_arg = "default-dark", frame_spec, keys_spec, start;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    if (a == "--presets") presets_dir = next();
    else if (a == "--layout") layout_arg = next();
    else if (a == "--theme") theme_arg = next();
    else if (a == "--frame") frame_spec = next();
    else if (a == "--keys") keys_spec = next();
    else if (!a.empty() && a[0] != '-' && start.empty()) start = a;
    else return usage();
  }

  App app;
  app.set_theme(theme_arg.c_str());
  if (!app.effects) app.set_theme("default-dark");
  rolltui_context_set_dir(app.ctx, presets_dir.data(), presets_dir.size());

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
      const std::string name = layout_arg.empty() ? std::string("explorer") : layout_arg;
      const bool path = name.find('/') != std::string::npos || name.find(".json") != std::string::npos;
      const std::string file = path ? name : presets_dir + "/layouts/" + name + ".json";
      bool ok = false;
      const std::string text = read_file(file, ok);
      if (ok) { loaded = load_layout_text(app.ctx, text, &rep); have = loaded != nullptr; }
      if (!ok) { rolltui_str_append(&layout_tried, "  missing ", 10);
                 rolltui_str_append(&layout_tried, file.data(), file.size()); }
    } else {
      RolltuiStr text{};
      if (rolltui_app_file(argv[0], "rolltui-explorer", "layout",
                           explorer_kAppFiles, explorer_kAppFileCount, &text, &layout_tried)) {
        loaded = load_layout_text(app.ctx, std::string(text.p ? text.p : "", text.n), &rep);
        have = loaded != nullptr;
      }
      rolltui_str_free(&text);
    }
  }
  if (!have) {
    // NAME WHAT WAS WANTED AND EVERY PLACE IT WAS SOUGHT. "no layout ()" was this message, and
    // an empty parenthesis is the standard this library enforces on everyone else, failed here.
    std::fprintf(stderr, "rolltui-explorer: cannot load its layout%s%s\n",
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
      ok = rolltui_app_file(argv[0], "rolltui-explorer", "bindings",
                            explorer_kAppFiles, explorer_kAppFileCount, &t, nullptr) != 0;
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
        std::fprintf(stderr, "rolltui-explorer: bindings/default.json: %s\n", why.c_str());
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
      std::fprintf(stderr, "rolltui-explorer: %s\n", say.c_str());
      rolltui_str_free(&say);
    }
    rolltui_gap_report_release(&gaps);
  }

  if (!frame_spec.empty()) {
    if (!parse_size(frame_spec, app.w, app.h)) return usage();
    app.prepare();
    if (!keys_spec.empty()) {
      for (const rolltui_selftest::Step& st : rolltui_selftest::scripted_keys(keys_spec, app.w, app.h))
        if (!st.tick) app.handle(st.ev);
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

  RolltuiTerminalOptions opts{};
  RolltuiTerminal* term = rolltui_terminal_new(STDIN_FILENO, STDOUT_FILENO, opts);
  if (!rolltui_terminal_is_tty(term)) {
    rolltui_terminal_free(term);
    std::fprintf(stderr, "not a terminal; use --frame WxH\n");
    return 1;
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
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
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
        term, 250,
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
    if (pending.resized) { app.w = pending.w; app.h = pending.h; }
  }
  rolltui_str_free(&out);
  rolltui_swap_free(swap);
  rolltui_terminal_free(term);
  return 0;
}
