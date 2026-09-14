// rolltui/tests/parity_test.cpp — CAN A HOST BUILD WHAT THE LIBRARY BUILDS?
//
// The library writes its own widget kinds inside itself, against every header it has. A host
// writes one against nine vtable slots and `rolltui/rolltui.h`. Nothing else in this tree asks
// whether those two are equally expressive, and the asymmetry is invisible until someone tries.
//
// THE PROBE: `menu` is reimplemented here as a HOST kind — `hostmenu` — through the public API
// alone, both are driven with the same events, and the two windows' PRESENTED BYTES are compared.
// Bytes rather than text, because SGR is where a wrong role hides: a text frame would call two
// differently-coloured screens identical.
//
// `menu` is the choice because it is the richest kind the library has: levels, a breadcrumb, a
// typed filter, choices with values, toggles, typed input fields, a back row, a scroll and its
// markers. An easy kind proves nothing.
//
// WHAT THE COMPARISON IS WORTH IS THE CONTROL. Each crippling below disables ONE slot of the
// host copy and asserts the frames diverge — so a pass means the two screens agree, rather than
// meaning the instrument sees nothing.
//
// WHERE PARITY DOES NOT HOLD IT IS ASSERTED AS A DIVERGENCE, not omitted: the editing wall and
// the degenerate-width wall are checked to still be walls, so the day one is fixed this suite
// says so out loud instead of quietly agreeing.
//
// The screen is `tandem`. Its menu file is a fixture and its layout is written below, because
// what is under test is the KIND rather than the loader — this is not a files-only claim.
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

// ONLY the public header, deliberately: this suite is a CONSUMER-shaped program, so what it can
// reach is what a host can reach. A suite that opts into an internal header would be measuring
// the library from inside the wall it is trying to see.
#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

#ifndef ROLLTUI_FIXTURE_DIR
#error "ROLLTUI_FIXTURE_DIR must point at rolltui/tests/fixtures"
#endif

namespace {

using testkit::check;

std::string read_all(const std::string& path) {
  std::string out;
  char buf[4096];
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return out;
  for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0;) out.append(buf, static_cast<std::size_t>(n));
  ::close(fd);
  return out;
}

const std::string& fixture_dir() {
  static const std::string d = std::string(ROLLTUI_FIXTURE_DIR) + "/tandem";
  return d;
}

// ============================================================================================
// THE HOST'S COPY OF `menu`, and the whole point is that everything below is written against
// `rolltui/rolltui.h` and the nine slots.
//
// WHAT A HOST LENDS ITS OWN WIDGET rather than asking the window for: the bindings table, the
// ambiguous-width setting and the preset directory. All three are the host's own state — it set
// them — so a plugin-facing accessor would be a second path to a pointer the host already holds.
// ============================================================================================

constexpr std::size_t kBackRow = static_cast<std::size_t>(-1);

// Which slot is disabled for a control run. NONE is the real widget.
enum class Cripple { None, ScrollExtent, Draw, Handle, Layout, Problem, NoteAt };

// The seven roles a menu draw needs, plus the two a name/value row splits into. A host cannot
// ASK which roles the library's menu draws with — `rolltui_windows_menu_roles` is internal — so
// this table is read off the role vocabulary by name and is the probe's one guess.
struct MenuRoleTable {
  unsigned char item = ROLLTUI_ROLE_MENU_ITEM;
  unsigned char selected = ROLLTUI_ROLE_MENU_SELECTED;
  unsigned char breadcrumb = ROLLTUI_ROLE_MENU_BREADCRUMB;
  unsigned char shortcut = ROLLTUI_ROLE_MENU_SHORTCUT;
  unsigned char text_muted = ROLLTUI_ROLE_TEXT_MUTED;
  unsigned char warning = ROLLTUI_ROLE_WARNING;
  unsigned char scroll_marker = ROLLTUI_ROLE_SCROLL_MARKER;
  unsigned char label = ROLLTUI_ROLE_LABEL;
  unsigned char value = ROLLTUI_ROLE_VALUE;
};

struct HostMenu {
  // ---- borrows the host lends, valid for the life of the app --------------------------------
  RolltuiWindows* windows = nullptr;
  const RolltuiBindings* bindings = nullptr;
  const RolltuiMenuActions* actions = nullptr;
  std::string dir;
  int ambiguous = 0;
  Cripple cripple = Cripple::None;

  // ---- the document -------------------------------------------------------------------------
  std::string source;
  RolltuiMenuItem root;  // OWNED: the C++ destructor releases the tree
  std::string origin;
  std::string problem;
  std::vector<std::string> notes;       // the file's own unknown keys and bad values
  std::vector<std::string> live_notes;  // items naming an action nothing declares

  // ---- the navigation state, which is the widget's own -------------------------------------
  std::vector<std::size_t> path;
  std::size_t sel = 0;
  int top = 0;
  std::string filter;
  std::vector<std::size_t> vis;

  // ---- geometry. TWO rects, because the menu hits over its window's inner rect and draws
  // inside a one-column inset of it. ---------------------------------------------------------
  RolltuiRect hit{};   // the window's inner rect
  RolltuiRect area{};  // where the rows are drawn

  MenuRoleTable roles;
  RolltuiDrawScratch* draw = nullptr;
  RolltuiUnicodeScratch* u = nullptr;
};

// ---- the tree, walked ------------------------------------------------------------------------

RolltuiMenuItem* level_of(HostMenu* m) {
  RolltuiMenuItem* it = &m->root;
  for (std::size_t k = 0; k < m->path.size(); ++k) {
    if (m->path[k] >= it->children.size()) break;
    it = &it->children[m->path[k]];
  }
  return it;
}

bool has_back(const HostMenu* m) { return !m->path.empty(); }
std::size_t first_child_row(const HostMenu* m) { return has_back(m) ? 1 : 0; }

bool contains_ci(const char* hay, std::size_t hn, const std::string& needle) {
  if (needle.empty()) return true;
  if (needle.size() > hn) return false;
  for (std::size_t i = 0; i + needle.size() <= hn; ++i) {
    std::size_t j = 0;
    for (; j < needle.size(); ++j) {
      const unsigned char a = static_cast<unsigned char>(hay[i + j]);
      const unsigned char b = static_cast<unsigned char>(needle[j]);
      const unsigned char la = (a >= 'A' && a <= 'Z') ? static_cast<unsigned char>(a + 32) : a;
      const unsigned char lb = (b >= 'A' && b <= 'Z') ? static_cast<unsigned char>(b + 32) : b;
      if (la != lb) break;
    }
    if (j == needle.size()) return true;
  }
  return false;
}

// The visible list: the way back first and unfiltered, then whatever the filter keeps.
std::size_t build_visible(HostMenu* m) {
  m->vis.clear();
  if (has_back(m)) m->vis.push_back(kBackRow);
  const RolltuiMenuItem* lv = level_of(m);
  for (std::size_t i = 0; i < lv->children.size(); ++i)
    if (contains_ci(lv->children[i].label.p, lv->children[i].label.n, m->filter)) m->vis.push_back(i);
  return m->vis.size();
}

// NULL for the back row: it stands for no item in anyone's tree.
RolltuiMenuItem* item_at(HostMenu* m, std::size_t i) {
  const std::size_t n = build_visible(m);
  if (i >= n || m->vis[i] == kBackRow) return nullptr;
  return &level_of(m)->children[m->vis[i]];
}

// The status row exists only while a filter is typed, at the bottom; the breadcrumb is the
// window's title, through the `title` slot below.
int status_rows(const HostMenu* m) { return m->area.h >= 2 && !m->filter.empty() ? 1 : 0; }
int item_rows(const HostMenu* m) { return m->area.h - status_rows(m); }

int imax(int a, int b) { return a > b ? a : b; }

void ensure_visible(HostMenu* m) {
  const int rows = item_rows(m);
  if (rows <= 0) {
    m->top = 0;
    return;
  }
  if (static_cast<int>(m->sel) < m->top) m->top = static_cast<int>(m->sel);
  if (static_cast<int>(m->sel) >= m->top + rows) m->top = static_cast<int>(m->sel) - rows + 1;
  const int n = static_cast<int>(build_visible(m));
  const int hi = imax(0, n - rows);
  if (m->top < 0) m->top = 0;
  if (m->top > hi) m->top = hi;
}

void clamp_selection(HostMenu* m) {
  const std::size_t n = build_visible(m);
  if (n == 0) m->sel = 0;
  else if (m->sel >= n) m->sel = n - 1;
  ensure_visible(m);
}

// ---- what a row says --------------------------------------------------------------------------

// `value_at` is the byte offset where the row's VALUE half begins, or 0 when the row is all
// name — the draw gives the two halves different foregrounds without building two strings.
std::string row_text(HostMenu* m, const RolltuiMenuItem* it, std::size_t* value_at) {
  std::string out;
  *value_at = 0;
  switch (static_cast<unsigned char>(it->kind)) {
    case ROLLTUI_MENU_TOGGLE:
      out += it->checked ? (m->ambiguous ? "[x] " : "\xE2\x98\x92 ") : (m->ambiguous ? "[ ] " : "\xE2\x98\x90 ");
      out += str_of(it->label);
      break;
    case ROLLTUI_MENU_INPUT:
      out += str_of(it->label);
      out += ": ";
      *value_at = out.size();
      out += str_of(it->value);
      break;
    default:
      out += str_of(it->label);
  }
  const RolltuiMenuItem* lv = level_of(m);
  if (static_cast<unsigned char>(lv->kind) == ROLLTUI_MENU_CHOICE &&
      rolltui_str_eq(&it->id, lv->value.p ? lv->value.p : "", lv->value.n)) {
    out = "\xE2\x80\xA2 " + out;  // • the current option
    if (*value_at != 0) *value_at += 2;
  }
  return out;
}

std::string breadcrumb(HostMenu* m, const std::string& base = "") {
  std::string out = base.empty() ? str_of(m->root.label) : base;
  const RolltuiMenuItem* it = &m->root;
  for (std::size_t k = 0; k < m->path.size(); ++k) {
    if (m->path[k] >= it->children.size()) break;
    it = &it->children[m->path[k]];
    if (!out.empty()) out += " \xE2\x80\xBA ";
    out += str_of(it->label);
  }
  return out;
}

// ---- the file, resolved by the host that shipped it -------------------------------------------

void refresh(HostMenu* m) {
  const std::string path = m->dir + "/menus/" + m->source + ".json";
  const std::string text = read_all(path);
  m->notes.clear();
  m->problem.clear();
  if (text.empty()) {
    m->origin.clear();
    m->problem = "no menu file '" + m->source + "' (looked for '" + path + "')";
    return;
  }
  m->origin = path;
  RolltuiMenuLoadReport rep{};
  const int ok = rolltui_menu_parse_json(text.data(), text.size(), &m->root, &rep);
  const std::string where = "menu file (" + m->origin + ")";
  if (!ok) {
    m->problem = where + " is unusable: " + str_of(rep.error);
  } else {
    for (std::size_t i = 0; i < rep.unknown_keys_n; ++i) m->notes.push_back(where + ": " + str_of(rep.unknown_keys[i]));
    for (std::size_t i = 0; i < rep.bad_values_n; ++i) m->notes.push_back(where + ": " + str_of(rep.bad_values[i]));
  }
  rolltui_menu_load_report_release(&rep);
}

void collect_action_notes(HostMenu* m, const RolltuiMenuItem* it) {
  if (it->action_name.n > 0 && !rolltui_bindings_has(m->bindings, it->action_name.p, it->action_name.n))
    m->live_notes.push_back("menu file (" + m->origin + "): item '" + str_of(it->id) + "' names the action '" +
                            str_of(it->action_name) + "', which no layout declares");
  for (std::size_t i = 0; i < it->children.size(); ++i) collect_action_notes(m, &it->children[i]);
}

// ---- the nine slots ---------------------------------------------------------------------------

void host_destroy(void* ctx) {
  HostMenu* m = static_cast<HostMenu*>(ctx);
  rolltui_draw_scratch_free(m->draw);
  rolltui_u_scratch_free(m->u);
  delete m;
}

int host_problem(void* ctx, RolltuiStr* out) {
  HostMenu* m = static_cast<HostMenu*>(ctx);
  if (m->cripple == Cripple::Problem) return 0;
  if (m->problem.empty()) return 0;
  rolltui_str_set(out, m->problem.data(), m->problem.size());
  return 1;
}

int host_note_at(void* ctx, std::size_t i, RolltuiStr* out) {
  HostMenu* m = static_cast<HostMenu*>(ctx);
  if (m->cripple == Cripple::NoteAt) return 0;
  if (i == 0) {
    m->live_notes.clear();
    collect_action_notes(m, &m->root);
  }
  if (i < m->notes.size()) {
    rolltui_str_set(out, m->notes[i].data(), m->notes[i].size());
    return 1;
  }
  const std::size_t j = i - m->notes.size();
  if (j < m->live_notes.size()) {
    rolltui_str_set(out, m->live_notes[j].data(), m->live_notes[j].size());
    return 1;
  }
  return 0;
}

void host_layout(void* ctx, const RolltuiResolvedNode* rn) {
  HostMenu* m = static_cast<HostMenu*>(ctx);
  if (m->cripple == Cripple::Layout) return;
  m->hit = rn->inner;
  rolltui_content_rect(rn, &m->area);
  // A menu shows an action's LIVE chords, so the tree is refreshed against the table that is
  // in force now rather than the one that was loaded.
  rolltui_menu_apply_shortcuts(&m->root, m->bindings);
  ensure_visible(m);
}

// The window's title: the author's, with the level's path after it.
int host_title(void* ctx, const char* given, size_t given_len, RolltuiStr* out) {
  HostMenu* m = static_cast<HostMenu*>(ctx);
  const std::string t = breadcrumb(m, std::string(given, given_len));
  rolltui_str_set(out, t.data(), t.size());
  return out->n != 0;
}

void host_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
  HostMenu* m = static_cast<HostMenu*>(ctx);
  host_layout(ctx, rn);
  if (m->cripple == Cripple::Draw) return;
  const RolltuiRect a = m->area;
  const int aw = m->ambiguous;
  if (a.w <= 0 || a.h <= 0) return;
  const RolltuiStyle* styles = rolltui_windows_styles(m->windows);
  if (!styles) return;
  auto style = [&](unsigned char role) { return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role); };
  auto put = [&](int x, int y, const std::string& s, RolltuiStyle st, int max_cells) {
    return rolltui_frame_put_text(f, m->draw, x, y, s.data(), s.size(), st, max_cells, aw, 0);
  };

  const std::size_t vis_n = build_visible(m);
  int y = a.y;
  if (status_rows(m)) put(a.x, a.y + a.h - 1, "/" + m->filter, style(m->roles.shortcut), a.w);
  const int rows = item_rows(m);
  if (vis_n == 0) {
    if (rows > 0)
      put(a.x, y, m->filter.empty() ? "(empty)" : "(no match for /" + m->filter + ")",
          style(m->roles.text_muted), a.w);
    return;
  }
  for (int r = 0; r < rows; ++r) {
    const std::size_t i = static_cast<std::size_t>(m->top + r);
    if (i >= vis_n) break;
    const RolltuiRect row_rect{a.x, y + r, a.w, 1};
    if (m->vis[i] == kBackRow) {
      const RolltuiStyle back = style(i == m->sel ? m->roles.selected : m->roles.shortcut);
      rolltui_frame_fill(f, m->draw, row_rect, back, nullptr, 0);
      put(a.x, y + r, "\xE2\x97\x82 Back", back, a.w);
      continue;
    }
    const RolltuiMenuItem* it = item_at(m, i);
    if (!it) break;
    const bool is_sel = i == m->sel;
    const RolltuiStyle base =
        style(is_sel ? m->roles.selected : (it->enabled ? m->roles.item : m->roles.text_muted));
    rolltui_frame_fill(f, m->draw, row_rect, base, nullptr, 0);

    // A ROW THAT CARRIES AN ANSWER IS A NAME AND A VALUE. Only the foreground comes from the
    // role — the row keeps its own background, so a selected row stays one solid block.
    const bool two_part =
        !is_sel && it->value.n != 0 &&
        (static_cast<unsigned char>(it->kind) == ROLLTUI_MENU_INPUT ||
         static_cast<unsigned char>(it->kind) == ROLLTUI_MENU_CHOICE);
    RolltuiStyle name_style = base;
    RolltuiStyle value_style = style(m->roles.value);
    name_style.bg = base.bg;
    value_style.bg = base.bg;

    std::size_t split = 0;
    const std::string line = row_text(m, it, &split);
    std::string right;
    if (static_cast<unsigned char>(it->kind) == ROLLTUI_MENU_CHOICE) right = str_of(it->value) + " \xE2\x96\xB8";
    else if (static_cast<unsigned char>(it->kind) == ROLLTUI_MENU_SUBMENU) right = "\xE2\x96\xB8";
    else if (it->shortcut.n) right = str_of(it->shortcut);

    const int rw = right.empty() ? 0 : rolltui_u_display_width(m->u, right.data(), right.size(), aw);
    const int left_max = right.empty() ? a.w : imax(a.w - rw - 1, 0);
    int used;
    if (two_part && split != 0 && split < line.size()) {
      used = put(a.x, y + r, line.substr(0, split), name_style, left_max);
      used += put(a.x + used, y + r, line.substr(split), value_style, imax(left_max - used, 0));
    } else {
      used = put(a.x, y + r, line, name_style, left_max);
    }
    if (rw > 0 && rw <= a.w) {
      // A CHOICE's right column is its current answer, so it is a value; a submenu's marker is
      // punctuation and stays the row's own colour.
      const RolltuiStyle rs =
          is_sel ? base
                 : (static_cast<unsigned char>(it->kind) == ROLLTUI_MENU_CHOICE
                        ? value_style
                        : (static_cast<unsigned char>(it->kind) == ROLLTUI_MENU_SUBMENU ? name_style
                                                                                        : style(m->roles.shortcut)));
      const int rx = imax(a.w - rw, used + 1);
      put(a.x + rx, y + r, right, rs, imax(a.w - rx, 0));
    }
  }
  if (a.w >= 1 && rows >= 1) {
    // ▲ and ▼ ARE EAST ASIAN AMBIGUOUS. `put_text` measures and will not cut a two-cell glyph into
    // a one-cell slot, so with `ambiguous_wide` on it lays down nothing here. The library's own menu
    // writes these through `rolltui_frame_put`, which takes the width as a parameter and forces one
    // — a call no host can make.
    const RolltuiStyle mark = style(m->roles.scroll_marker);
    if (m->top > 0) put(a.x + a.w - 1, y, "\xE2\x96\xB2", mark, 1);
    if (static_cast<std::size_t>(m->top + rows) < vis_n) put(a.x + a.w - 1, y + rows - 1, "\xE2\x96\xBC", mark, 1);
  }
}

int host_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  HostMenu* m = static_cast<HostMenu*>(ctx);
  if (m->cripple == Cripple::ScrollExtent) return 0;
  if (axis != ROLLTUI_AXIS_VERTICAL) return 0;
  out->first = static_cast<std::size_t>(m->top < 0 ? 0 : m->top);
  const int r = item_rows(m);
  out->visible = static_cast<std::size_t>(r < 0 ? 0 : r);
  out->total = build_visible(m);
  return 1;
}

// ---- acting and navigating ---------------------------------------------------------------------

void descend(HostMenu* m, std::size_t child) {
  m->path.push_back(child);
  m->filter.clear();
  m->sel = first_child_row(m);
  m->top = 0;
  const RolltuiMenuItem* lv = level_of(m);
  if (static_cast<unsigned char>(lv->kind) == ROLLTUI_MENU_CHOICE)
    for (std::size_t i = 0; i < lv->children.size(); ++i)
      if (rolltui_str_eq(&lv->children[i].id, lv->value.p ? lv->value.p : "", lv->value.n)) {
        m->sel = first_child_row(m) + i;
        break;
      }
  clamp_selection(m);
}

bool ascend(HostMenu* m) {
  if (m->path.empty()) return false;
  const std::size_t was = m->path.back();
  m->path.pop_back();
  m->filter.clear();
  const RolltuiMenuItem* lv = level_of(m);
  const std::size_t n = lv->children.size();
  m->sel = n == 0 ? 0 : first_child_row(m) + (was < n - 1 ? was : n - 1);
  m->top = 0;
  clamp_selection(m);
  return true;
}

// WHAT A ROW BEING ACTED ON MEANS, which is the widget's other output and is not on the frame.
// The library hands it back as a `RolltuiMenuEvent`; a host kind's `handle` slot answers only
// "consumed", so a host that wants the outcome keeps it and reads it by name. The kinds are the
// library's own constants so the two can be compared without a translation nobody would trust.
struct Outcome {
  unsigned char kind = ROLLTUI_MENU_EVENT_NONE;
  std::string id, value;
  bool checked = false;
};

std::string outcome_text(unsigned char kind, const std::string& id, const std::string& value, bool checked) {
  if (kind == ROLLTUI_MENU_EVENT_NONE) return "";
  return std::to_string(kind) + " " + id + " " + value + (checked ? " checked" : "");
}

void act(HostMenu* m, std::size_t i, Outcome* out) {
  if (i < build_visible(m) && m->vis[i] == kBackRow) {
    ascend(m);
    return;
  }
  RolltuiMenuItem* it = item_at(m, i);
  if (!it || !it->enabled) return;
  RolltuiMenuItem* lv = level_of(m);
  if (static_cast<unsigned char>(lv->kind) == ROLLTUI_MENU_CHOICE) {
    rolltui_str_set(&lv->value, it->id.p ? it->id.p : "", it->id.n);
    out->kind = ROLLTUI_MENU_EVENT_CHOOSE;
    out->id = str_of(lv->id);
    out->value = str_of(it->id);
    ascend(m);
    return;
  }
  switch (static_cast<unsigned char>(it->kind)) {
    case ROLLTUI_MENU_ACTION:
      out->kind = ROLLTUI_MENU_EVENT_ACTIVATE;
      out->id = str_of(it->id);
      return;
    case ROLLTUI_MENU_TOGGLE:
      it->checked = !it->checked;
      out->kind = ROLLTUI_MENU_EVENT_TOGGLE;
      out->id = str_of(it->id);
      out->checked = it->checked != 0;
      return;
    case ROLLTUI_MENU_SUBMENU:
    case ROLLTUI_MENU_CHOICE:
      build_visible(m);
      descend(m, m->vis[i]);
      return;
    case ROLLTUI_MENU_INPUT:
      // THE WALL. Opening a typed field needs the library's line editor, and every call that
      // drives one — `rolltui_input_new`, `_set_text`, `_select_all`, `_handle`, `_layout`,
      // `_draw`, and the `rolltui_check_input` that validates each keystroke as a PREFIX of
      // some valid value — is internal. A host kind can select the row and can do nothing with
      // it, which is what the divergence assertion below measures.
      m->sel = i;
      return;
    default:
      return;
  }
}

void move_to(HostMenu* m, std::size_t i, std::size_t n) {
  if (n == 0) {
    m->sel = 0;
    return;
  }
  m->sel = i < n - 1 ? i : n - 1;
  ensure_visible(m);
}
// The cursor never rests on a disabled row: onward in `dir`, and back the other way at the end.
void settle(HostMenu* m, int dir) {
  const long long n = static_cast<long long>(build_visible(m));
  auto ok = [&](long long i) { const RolltuiMenuItem* it = item_at(m, static_cast<std::size_t>(i)); return it == nullptr || it->enabled; };
  long long s = static_cast<long long>(m->sel);
  if (n == 0) return;
  while (s >= 0 && s < n && !ok(s)) s += dir;
  if (s < 0 || s >= n) { s = static_cast<long long>(m->sel); while (s >= 0 && s < n && !ok(s)) s -= dir; if (s < 0 || s >= n) return; }
  m->sel = static_cast<std::size_t>(s);
  ensure_visible(m);
}

bool action_is(const char* a, std::size_t n, const char* name) {
  return name && std::strlen(name) == n && std::memcmp(a, name, n) == 0;
}

// A codepoint as UTF-8. `rolltui_u_append_utf8` is internal, so every host with a typed filter
// writes this.
std::string utf8_of(RolltuiCodepoint c) {
  std::string s;
  if (c < 0x80) {
    s += static_cast<char>(c);
  } else if (c < 0x800) {
    s += static_cast<char>(0xC0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    s += static_cast<char>(0xE0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (c >> 18));
    s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  }
  return s;
}

void handle_key(HostMenu* m, const RolltuiChord& k, Outcome* out) {
  const RolltuiMenuActions* A = m->actions;
  const bool text = k.key == ROLLTUI_KEY_CHAR && !k.ctrl && !k.alt && k.ch >= 0x20 && k.ch != 0x7F;
  if (text) {
    m->filter += utf8_of(k.ch);
    // The first MATCH, not the first row: typing is looking for something, and the way out is
    // not one of the things being looked for.
    m->sel = first_child_row(m);
    m->top = 0;
    clamp_selection(m);
    return;
  }
  std::size_t alen = 0;
  const char* a = rolltui_bindings_action_for(m->bindings, &k, "menu", 4, &alen);
  if (!a) return;
  const std::size_t n = build_visible(m);
  const std::size_t page = static_cast<std::size_t>(imax(item_rows(m), 1));
  if (action_is(a, alen, A->up)) { move_to(m, m->sel == 0 ? 0 : m->sel - 1, n); return settle(m, -1); }
  if (action_is(a, alen, A->down)) { move_to(m, m->sel + 1, n); return settle(m, 1); }
  if (action_is(a, alen, A->page_up)) { move_to(m, m->sel < page ? 0 : m->sel - page, n); return settle(m, -1); }
  if (action_is(a, alen, A->page_down)) { move_to(m, m->sel + page, n); return settle(m, 1); }
  if (action_is(a, alen, A->first)) { move_to(m, first_child_row(m), n); return settle(m, 1); }
  if (action_is(a, alen, A->last)) { move_to(m, n == 0 ? 0 : n - 1, n); return settle(m, -1); }
  if (action_is(a, alen, A->activate)) return act(m, m->sel, out);
  if (action_is(a, alen, A->descend)) {
    const RolltuiMenuItem* it = item_at(m, m->sel);
    if (it && it->enabled && (static_cast<unsigned char>(it->kind) == ROLLTUI_MENU_SUBMENU ||
                              static_cast<unsigned char>(it->kind) == ROLLTUI_MENU_CHOICE)) {
      build_visible(m);
      descend(m, m->vis[m->sel]);
    }
    return;
  }
  if (action_is(a, alen, A->ascend)) {
    if (!m->filter.empty()) {
      m->filter.clear();
      clamp_selection(m);
      return;
    }
    ascend(m);
    return;
  }
  if (action_is(a, alen, A->back)) {
    if (!m->filter.empty()) {
      m->filter.clear();
      clamp_selection(m);
      return;
    }
    if (ascend(m)) return;
    out->kind = ROLLTUI_MENU_EVENT_CLOSED;
    return;
  }
  if (action_is(a, alen, A->erase)) {
    if (!m->filter.empty()) {
      // One grapheme off the end. `rolltui_u_graphemes` is internal, so the public fit function
      // stands in: the bytes that fill one cell fewer than the whole string. IT IS NOT THE SAME
      // FUNCTION — a combining mark has no width to subtract and an emoji has two — so this is
      // right for an ASCII filter and wrong in general. The substitution is the copy's defect,
      // not the library's, and it is here because no public call answers the real question.
      int cells = 0;
      const int whole = rolltui_u_display_width(m->u, m->filter.data(), m->filter.size(), m->ambiguous);
      const std::size_t keep = whole <= 1 ? 0
                                          : rolltui_u_fit(m->u, m->filter.data(), m->filter.size(), whole - 1,
                                                          m->ambiguous, &cells);
      m->filter.resize(keep);
      clamp_selection(m);
    }
    return;
  }
}

void handle_mouse(HostMenu* m, const RolltuiMouseEvent& e, Outcome* out) {
  if (e.kind == RolltuiMouseEvent::Kind::WheelUp) {
    if (m->sel > 0) {
      --m->sel;
      ensure_visible(m);
    }
    return;
  }
  if (e.kind == RolltuiMouseEvent::Kind::WheelDown) {
    const std::size_t n = build_visible(m);
    if (n && m->sel + 1 < n) {
      ++m->sel;
      ensure_visible(m);
    }
    return;
  }
  if (e.kind != RolltuiMouseEvent::Kind::Press || e.button != 1) return;
  if (!(e.x >= m->hit.x && e.y >= m->hit.y && e.x < m->hit.x + m->hit.w && e.y < m->hit.y + m->hit.h)) return;
  const int first_item_row = m->hit.y;
  if (e.y < first_item_row || e.y >= first_item_row + item_rows(m)) return;
  const std::size_t idx = static_cast<std::size_t>(m->top) + static_cast<std::size_t>(e.y - first_item_row);
  if (idx >= build_visible(m)) return;
  m->sel = idx;
  act(m, m->sel, out);
}

// The one outcome the last event produced, kept where a host can read it — the vtable's
// `handle` answers only "consumed".
Outcome g_host_outcome;

int host_handle(void* ctx, const RolltuiEvent* e) {
  HostMenu* m = static_cast<HostMenu*>(ctx);
  if (m->cripple == Cripple::Handle) return 0;
  g_host_outcome = Outcome{};
  if (e->kind == ROLLTUI_EVENT_KEY) {
    handle_key(m, e->key, &g_host_outcome);
    return 1;
  }
  if (e->kind == ROLLTUI_EVENT_MOUSE) {
    handle_mouse(m, e->mouse, &g_host_outcome);
    return 1;
  }
  if (e->kind == ROLLTUI_EVENT_PASTE) {
    for (std::size_t i = 0; i < e->text_len; ++i) {
      const unsigned char c = static_cast<unsigned char>(e->text[i]);
      if (c >= 0x20 && c != 0x7F) {
        RolltuiChord k{};
        k.key = ROLLTUI_KEY_CHAR;
        k.ch = c;
        handle_key(m, k, &g_host_outcome);
      }
    }
    return 1;
  }
  return 0;
}

constexpr RolltuiWidgetPlugin kHostMenuPlugin = {
    /*destroy=*/host_destroy,
    /*layout=*/host_layout,
    /*draw=*/host_draw,
    /*problem=*/host_problem,
    /*note_at=*/host_note_at,
    /*desired_outer=*/nullptr,  // the layout decides, exactly as the library's menu leaves it
    /*handle=*/host_handle,
    /*scroll_extent=*/host_scroll_extent,
    /*scroll_to=*/nullptr,  // a menu's scroll follows its selection: an accurate bar, not a handle
    /*title=*/host_title,
};

struct HostFactoryCtx {
  RolltuiWindows* windows = nullptr;
  const RolltuiBindings* bindings = nullptr;
  std::string dir;
  int ambiguous = 0;
  Cripple cripple = Cripple::None;
};

RolltuiWidget host_menu_factory(void* ctx, RolltuiWindows* w, const char* content, std::size_t len) {
  HostFactoryCtx* fc = static_cast<HostFactoryCtx*>(ctx);
  const char* source = nullptr;
  std::size_t source_len = 0;
  unsigned char problem = 0;
  RolltuiStr why{};
  if (!rolltui_content_parse(rolltui_windows_context(w), content, len, nullptr, nullptr, nullptr, nullptr, &source,
                             &source_len, &problem, &why)) {
    rolltui_str_free(&why);
    return RolltuiWidget{};
  }
  rolltui_str_free(&why);
  HostMenu* m = new HostMenu();
  m->windows = fc->windows;
  m->bindings = fc->bindings;
  m->actions = rolltui_menu_default_actions();
  m->dir = fc->dir;
  m->ambiguous = fc->ambiguous;
  m->cripple = fc->cripple;
  m->source.assign(source, source_len);
  m->draw = rolltui_draw_scratch_new();
  m->u = rolltui_u_scratch_new();
  refresh(m);
  return RolltuiWidget{&kHostMenuPlugin, m};
}

// ============================================================================================
// ONE SCREEN, TWICE — the same layout with one content string changed.
// ============================================================================================

std::string layout_json(const char* content) {
  return std::string(R"({"name": "tandem", "min_width": 0, "min_height": 0, "focus": "pane",
  "actions": { "app.tandem": "run the pair" },
  "root": { "id": "pane", "content": ")") +
         content + R"(", "border": "single", "title": "tandem", "focusable": true } })";
}

constexpr int kW = 46, kH = 12;
constexpr const char* kTheme = "default-dark";
constexpr const char* kHostKind = "hostmenu";
constexpr const char* kHostDescribes = "a host's copy of `menu`";
constexpr const char* kWindow = "pane";
constexpr const char* kSource = "tandem";
constexpr const char* kShortcutAction = "app.tandem";
constexpr const char* kShortcutChord = "ctrl+t";

struct Side {
  RolltuiContext* ctx = nullptr;
  RolltuiWindows* windows = nullptr;
  RolltuiWindowStack* stack = nullptr;
  RolltuiBindings* bindings = nullptr;
  RolltuiLayout* layout = nullptr;
  RolltuiComposeScratch* compose = nullptr;
  RolltuiSwap* swap = nullptr;
  RolltuiEffectMap* effects = nullptr;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  HostFactoryCtx factory_ctx;
  bool host_side = false;
  int ambiguous = 0;
  int w = kW, h = kH;
  // WHAT THE WIDGET ANSWERED, per step — the output that is not on the frame.
  std::vector<std::string> outcomes;

  void open(bool host, Cripple cripple = Cripple::None, int amb = 0, int width = kW, int height = kH) {
    host_side = host;
    ambiguous = amb;
    w = width;
    h = height;
    ctx = rolltui_context_new();
    rolltui_context_set_library_defaults(ctx);
    rolltui_context_set_dir(ctx, fixture_dir().data(), fixture_dir().size());
    effects = rolltui_theme_builtin_fill(kTheme, std::strlen(kTheme), styles, ROLLTUI_ROLE_COUNT);

    windows = rolltui_windows_new(ctx);
    stack = rolltui_window_stack_new();
    bindings = rolltui_bindings_clone(rolltui_bindings_default(ctx));

    if (host_side) {
      rolltui_widget_kind_register(ctx, kHostKind, std::strlen(kHostKind), ROLLTUI_SOURCE_REQUIRED, kHostDescribes,
                                   std::strlen(kHostDescribes));
      factory_ctx = HostFactoryCtx{windows, bindings, fixture_dir(), ambiguous, cripple};
      rolltui_context_register_kind(ctx, kHostKind, std::strlen(kHostKind), host_menu_factory, &factory_ctx, nullptr);
    }

    const std::string text = layout_json(host_side ? "hostmenu:tandem" : "menu:tandem");
    RolltuiLayoutReport rep{};
    layout = rolltui_load_layout_text(text.data(), text.size(), nullptr, 0, nullptr, &rep);
    rolltui_layout_report_release(&rep);
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));

    std::size_t an = 0;
    const RolltuiLayoutAction* av = rolltui_layout_actions(layout, &an);
    rolltui_bindings_declare(bindings, av, an, nullptr, 0);
    // A declared action with a chord is what puts a live shortcut in the right-hand column.
    RolltuiChord chord{};
    rolltui_chord_parse(kShortcutChord, std::strlen(kShortcutChord), &chord);
    rolltui_bindings_bind(bindings, kShortcutAction, std::strlen(kShortcutAction), &chord, nullptr, nullptr);

    RolltuiWidgetEnv env{};
    env.ambiguous_wide = static_cast<unsigned char>(ambiguous);
    rolltui_context_set_env(ctx, &env);
    rolltui_context_set_bindings(ctx, bindings);
    rolltui_windows_sync(windows, stack);
    rolltui_windows_autosize(windows, stack, rect());
    rolltui_windows_layout(windows, stack, rect());

    compose = rolltui_compose_scratch_new();
    swap = rolltui_swap_new(w, h, *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BACKGROUND));
  }

  RolltuiRect rect() const { return RolltuiRect{0, 0, w, h}; }

  RolltuiMenu* library_menu() { return rolltui_windows_menu(windows, kSource, std::strlen(kSource)); }

  // THE SAME EVENT, delivered by each side's own natural route. The library's `menu` kind
  // leaves `handle` NULL — a host drives the menu object it can ask for by name — while a host
  // kind has only the plugin, so its events arrive through the window.
  void send(const RolltuiEvent& e) {
    if (host_side) {
      rolltui_windows_handle(windows, kWindow, std::strlen(kWindow), &e);
      outcomes.push_back(outcome_text(g_host_outcome.kind, g_host_outcome.id, g_host_outcome.value,
                                      g_host_outcome.checked));
    } else {
      RolltuiMenuEvent out{};
      rolltui_menu_handle(library_menu(), &e, bindings, rolltui_menu_default_actions(), &out);
      outcomes.push_back(outcome_text(out.kind, str_of(out.id), str_of(out.value), out.checked != 0));
      rolltui_menu_event_release(&out);
    }
  }

  static void draw_slot(void* c, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
    Side* s = static_cast<Side*>(c);
    rolltui_windows_draw(s->windows, rn, f, s->styles, rolltui_windows_default_roles());
  }

  std::string present() {
    rolltui_windows_layout(windows, stack, rect());
    RolltuiFrame* f =
        rolltui_swap_begin(swap, w, h, *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BACKGROUND));
    rolltui_window_stack_compose(stack, f, rect(), styles, rolltui_layout_default_roles(), draw_slot, this,
                                 ambiguous, compose);
    RolltuiStr bytes{};
    rolltui_swap_present(swap, ROLLTUI_DEPTH_TRUECOLOR, &bytes);
    std::string out(bytes.p ? bytes.p : "", bytes.n);
    rolltui_str_free(&bytes);
    rolltui_swap_invalidate(swap);  // every present is a full paint, so two are comparable
    return out;
  }

  std::string frame_text() {
    rolltui_windows_layout(windows, stack, rect());
    RolltuiFrame* f =
        rolltui_swap_begin(swap, w, h, *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, ROLLTUI_ROLE_BACKGROUND));
    rolltui_window_stack_compose(stack, f, rect(), styles, rolltui_layout_default_roles(), draw_slot, this,
                                 ambiguous, compose);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    std::string out(text.p ? text.p : "", text.n);
    rolltui_str_free(&text);
    rolltui_swap_invalidate(swap);
    return out;
  }

  // The report with the window's own prefix removed, since the two windows differ only by the
  // content string that names the kind.
  std::vector<std::string> reports() {
    rolltui_windows_sync(windows, stack);
    std::vector<std::string> out;
    for (std::size_t i = 0, n = rolltui_windows_report_count(windows); i < n; ++i) {
      std::size_t len = 0;
      const char* p = rolltui_windows_report_at(windows, i, &len);
      std::string s(p ? p : "", len);
      const std::size_t at = s.find("'): ");
      out.push_back(at == std::string::npos ? s : s.substr(at + 4));
    }
    return out;
  }

  ~Side() {
    rolltui_swap_free(swap);
    rolltui_compose_scratch_free(compose);
    rolltui_bindings_free(bindings);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_layout_free(layout);
    rolltui_effect_map_free(effects);
    rolltui_context_free(ctx);
  }
};

// ---- the script --------------------------------------------------------------------------------

RolltuiEvent key(unsigned char k, RolltuiCodepoint ch = 0) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = k;
  e.key.ch = ch;
  return e;
}

RolltuiEvent ch(char c) { return key(ROLLTUI_KEY_CHAR, static_cast<RolltuiCodepoint>(c)); }

RolltuiEvent click(int x, int y) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_MOUSE;
  e.mouse.kind = RolltuiMouseEvent::Kind::Press;
  e.mouse.button = 1;
  e.mouse.x = x;
  e.mouse.y = y;
  return e;
}

// A SENTINEL EVENT WOULD BE A SILENT NO-OP: `ROLLTUI_EVENT_KEY` is 0, so "kind != 0" reads as
// "this step carries an event" and means "this step is not a key press". The flag is explicit
// for that reason — one step opens the script with no event at all, and every other one sends.
struct Step {
  const char* what;
  RolltuiEvent e;
  bool sends = true;
};

// EVERY SHAPE THE WIDGET HAS, in one pass: moving, paging, the ends, a filter and its erase, a
// toggle, a submenu with its breadcrumb and its back row, a choice that keeps its answer, a
// click, and enough movement to scroll both markers into view.
std::vector<Step> script() {
  std::vector<Step> s;
  const auto step = [&s](const char* what, RolltuiEvent e) { s.push_back({what, e, true}); };
  s.push_back({"a fresh screen", RolltuiEvent{}, /*sends=*/false});
  step("down once", key(ROLLTUI_KEY_DOWN));
  step("down twice", key(ROLLTUI_KEY_DOWN));
  step("end", key(ROLLTUI_KEY_END));
  step("page up", key(ROLLTUI_KEY_PAGEUP));
  step("home", key(ROLLTUI_KEY_HOME));
  step("typing i", ch('i'));
  step("typing it", ch('t'));
  step("erase", key(ROLLTUI_KEY_BACKSPACE));
  step("escape clears the filter", key(ROLLTUI_KEY_ESCAPE));
  // TO A ROW BY NAME, NOT BY COUNTING. A filter that leaves one match puts the selection on it
  // whatever moved before, so a step added later cannot silently retarget every step after it.
  step("filtering to the toggle: t", ch('t'));
  step("filtering to the toggle: tr", ch('r'));
  step("filtering to the toggle: tra", ch('a'));
  step("enter toggles it", key(ROLLTUI_KEY_ENTER));
  step("escape clears the filter again", key(ROLLTUI_KEY_ESCAPE));
  step("filtering to the choice: s", ch('s'));
  step("filtering to the choice: sh", ch('h'));
  step("right descends into the choice", key(ROLLTUI_KEY_RIGHT));
  step("down inside the choice", key(ROLLTUI_KEY_DOWN));
  step("enter chooses and returns", key(ROLLTUI_KEY_ENTER));
  step("end", key(ROLLTUI_KEY_END));
  step("page up", key(ROLLTUI_KEY_PAGEUP));
  step("filtering to the submenu: a", ch('a'));
  step("filtering to the submenu: ab", ch('b'));
  step("filtering to the submenu: abo", ch('o'));
  step("enter descends into it", key(ROLLTUI_KEY_ENTER));
  step("down inside the submenu", key(ROLLTUI_KEY_DOWN));
  step("a click on the last row", click(4, 4));  // items start on the area's first row
  step("left ascends", key(ROLLTUI_KEY_LEFT));
  step("page down", key(ROLLTUI_KEY_PAGEDOWN));
  step("page down again", key(ROLLTUI_KEY_PAGEDOWN));
  step("home", key(ROLLTUI_KEY_HOME));
  return s;
}

// Where two byte streams first differ, as a sentence an assertion can carry.
std::string first_difference(const std::string& a, const std::string& b) {
  if (a == b) return "";
  std::size_t i = 0;
  while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
  const auto window = [](const std::string& s, std::size_t at) {
    std::string out;
    for (std::size_t k = at; k < s.size() && k < at + 24; ++k) {
      const unsigned char c = static_cast<unsigned char>(s[k]);
      if (c == 0x1B) out += "\\e";
      else if (c < 0x20) out += '.';
      else out += static_cast<char>(c);
    }
    return out;
  };
  return " at byte " + std::to_string(i) + ": library [" + window(a, i) + "] host [" + window(b, i) + "]";
}

// WHAT ONE RUN OF THE SCRIPT MEASURES. `matched` is the parity number; `moved` is what says the
// script did anything at all — the count of DISTINCT frames the library drew. A script that
// moves nothing makes two frozen screens agree, and that is a pass no assertion can tell from a
// real one.
struct Run {
  int matched = 0;
  int moved = 0;
  int acted = 0;  // steps whose outcome was something rather than nothing
  bool outcomes_agree = false;
  std::string why;
};

Run drive(Cripple cripple, int amb = 0, int width = kW, int height = kH) {
  Side lib, host;
  lib.open(false, Cripple::None, amb, width, height);
  host.open(true, cripple, amb, width, height);
  Run out;
  std::vector<std::string> seen;
  for (const Step& s : script()) {
    if (s.sends) {
      lib.send(s.e);
      host.send(s.e);
    }
    const std::string a = lib.present();
    const std::string b = host.present();
    if (a == b) ++out.matched;
    else if (out.why.empty()) out.why = std::string(s.what) + first_difference(a, b);
    bool fresh = true;
    for (const std::string& p : seen)
      if (p == a) {
        fresh = false;
        break;
      }
    if (fresh) {
      seen.push_back(a);
      ++out.moved;
    }
  }
  out.outcomes_agree = lib.outcomes == host.outcomes;
  for (const std::string& o : lib.outcomes)
    if (!o.empty()) ++out.acted;
  return out;
}

}  // namespace

int main() {
  const int steps = static_cast<int>(script().size());

  // ---- 1. PARITY, frame for frame --------------------------------------------------------------
  {
    const Run r = drive(Cripple::None);
    // THE SCRIPT MUST MOVE THE SCREEN, and this is asserted BEFORE parity because a script that
    // moves nothing makes two frozen screens agree and every assertion below passes for free.
    // (`ROLLTUI_EVENT_KEY` is 0, so a "does this step carry an event?" test written as `kind != 0`
    // silently delivers no key at all — a real green with nothing behind it.)
    check(r.moved >= steps / 2,
          "the script drives the screen: " + std::to_string(r.moved) + " of " + std::to_string(steps) +
              " frames are distinct, so an agreement below is two screens agreeing rather than two "
              "frozen ones");
    check(r.matched == steps,
          "a host's copy of `menu` draws the SAME BYTES as the library's, over " + std::to_string(steps) +
              " steps of one script (" + std::to_string(r.matched) + " matched)" +
              (r.why.empty() ? "" : " — " + r.why));
    // …AND THE OTHER OUTPUT, which is not on the frame. A menu answers a host with what was
    // activated, toggled or chosen, and two widgets that draw alike while answering differently
    // are not the same widget.
    check(r.acted >= 3 && r.outcomes_agree,
          "…and the two answer the host the same way — the same activate / toggle / choose, in the "
          "same order, on the same steps (" + std::to_string(r.acted) + " of " + std::to_string(steps) +
              " steps produced one)");
  }

  // ---- 2. THE CONTROL: the comparison sees something --------------------------------------------
  //
  // A parity assertion that cannot fail is not evidence. Each run below disables ONE slot of the
  // host copy and nothing else; the frames must stop agreeing.
  {
    struct Case {
      Cripple c;
      const char* slot;
    };
    const Case cases[] = {
        {Cripple::Draw, "draw"},
        {Cripple::Layout, "layout"},
        {Cripple::Handle, "handle"},
        {Cripple::ScrollExtent, "scroll_extent"},
    };
    for (const Case& k : cases) {
      const Run r = drive(k.c);
      check(r.matched < steps, std::string("CONTROL: with `") + k.slot + "` crippled the frames diverge (" +
                                   std::to_string(r.matched) + "/" + std::to_string(steps) + " still matched)");
    }
    // …and `scroll_extent` is the sharpest of the four, because it changes nothing the widget
    // draws: the WINDOW stops drawing a scrollbar. A slot answered wrongly shows up even where the
    // widget's own cells are byte-identical.
    const Run r = drive(Cripple::ScrollExtent);
    check(r.why.find("a fresh screen") == 0,
          "CONTROL: a silent `scroll_extent` costs the window its scrollbar from the very first frame — " + r.why);
    // …and the outcome comparison is armed by the same means: a host that consumes nothing answers
    // nothing, so the two sequences must stop agreeing.
    check(!drive(Cripple::Handle).outcomes_agree,
          "CONTROL: with `handle` crippled the host answers nothing and the two outcome sequences part");
  }

  // ---- 3. THE REPORT: `problem` and `note_at` answer the same -----------------------------------
  {
    Side lib, host;
    lib.open(false);
    host.open(true);
    const std::vector<std::string> a = lib.reports(), b = host.reports();
    check(!a.empty() && a == b,
          "the two kinds report the SAME notes — the menu file's unknown key and the item naming an "
          "action nothing declares (" +
              std::to_string(a.size()) + " each)");
    bool unknown_key = false, undeclared = false;
    for (const std::string& s : a) {
      if (s.find("colour") != std::string::npos) unknown_key = true;
      if (s.find("which no layout declares") != std::string::npos) undeclared = true;
    }
    check(unknown_key && undeclared, "…and both kinds of note are among them, so the comparison is not of two empties");

    Side quiet;
    quiet.open(true, Cripple::NoteAt);
    check(quiet.reports().empty(), "CONTROL: with `note_at` silent the host's window reports nothing at all");
  }

  // ---- 4. THE EDITING WALL, asserted as a divergence ---------------------------------------------
  //
  // The library's menu opens a typed field on Enter and draws a line editor in the row. A host kind
  // cannot: `rolltui_input_new`, `_set_text`, `_select_all`, `_handle`, `_layout` and `_draw` are
  // internal, and so is `rolltui_check_input`, which is what refuses a keystroke that could not
  // still become a valid value. Asserted so that the day the wall moves this suite says so instead
  // of quietly agreeing.
  {
    Side lib, host;
    lib.open(false);
    host.open(true);
    // Home, then four rows down: `Run the pair`, `Names an undeclared action`, `Trace`, `Shading`,
    // and the fifth row is `Width`, the int field.
    const RolltuiEvent to_the_field[] = {key(ROLLTUI_KEY_HOME), key(ROLLTUI_KEY_DOWN), key(ROLLTUI_KEY_DOWN),
                                         key(ROLLTUI_KEY_DOWN), key(ROLLTUI_KEY_DOWN)};
    for (const RolltuiEvent& e : to_the_field) {
      lib.send(e);
      host.send(e);
    }
    check(lib.present() == host.present(), "the row holding the typed field draws the same on both sides");

    const RolltuiEvent enter = key(ROLLTUI_KEY_ENTER);
    lib.send(enter);
    host.send(enter);
    const std::string a = lib.frame_text(), b = host.frame_text();
    check(a != b, "WALL: Enter opens a typed field on the library's kind and the host's copy cannot follow");
    const std::size_t at = a.find("editing \xE2\x80\x94 ");
    check(at != std::string::npos && b.find("editing") == std::string::npos,
          "…and the difference is exactly the editor: the library's frame opens a field and says what it "
          "expects [" + a.substr(at == std::string::npos ? 0 : at, 44) + "], the host's has no field to open");
  }

  // ---- 5. THE DEGENERATE-WIDTH WALL --------------------------------------------------------------
  //
  // Three of the library's own kinds compute an inset from `rn->node->border`. A host cannot:
  // `RolltuiLayoutNode` is opaque in the public header. The public stand-in `rolltui_content_rect`
  // applies the inset only when the inner rect is at least 3 columns wide, while the menu kind
  // insets unconditionally and then declines to draw at a width of zero. So the two agree from an
  // inner width of 3 up and part company below it — which is exactly the boundary asserted here,
  // because a wall whose CAUSE is assumed is a wall nobody has actually found.
  {
    const Run narrow = drive(Cripple::None, 0, /*width=*/4, kH);  // inner 2: the library draws nothing
    check(narrow.matched < steps,
          "WALL: at an inner width of 2 the two kinds part company — a host cannot read the border its "
          "window has, and the public inset rule is not the one the library's menu uses — " + narrow.why);
    const Run wide_enough = drive(Cripple::None, 0, /*width=*/5, kH);  // inner 3: both inset by one
    check(wide_enough.matched == steps,
          "…and one column wider they agree again, which pins the cause to `rolltui_content_rect`'s "
          "three-column guard rather than to narrowness in general" +
              (wide_enough.why.empty() ? "" : " — " + wide_enough.why));
    const Run short_one = drive(Cripple::None, 0, kW, /*height=*/3);
    check(short_one.matched == steps,
          "…and it is the WIDTH alone: a three-row window of full width still matches frame for frame" +
              (short_one.why.empty() ? "" : " — " + short_one.why));
  }

  // ---- 6. THE FORCED-WIDTH WALL -------------------------------------------------------------------
  //
  // The library's menu writes its scroll markers with `rolltui_frame_put`, which takes the cell width
  // as a PARAMETER. `▲` and `▼` are East Asian ambiguous, so on a wide-ambiguous terminal they are two
  // cells and the library writes them into one anyway. `rolltui_frame_put` is internal; the public
  // `rolltui_frame_put_text` MEASURES, refuses to cut a two-cell glyph into a one-cell slot, and lays
  // down nothing.
  {
    const Run amb = drive(Cripple::None, /*ambiguous=*/1, kW, kH);
    check(amb.matched < steps,
          "WALL: with ambiguous-wide on, the library's scroll marker is written at a forced width a host "
          "cannot ask for — " + amb.why);
    // The marker is the WHOLE of it: a window tall enough to hold every row draws no marker, and the
    // same ambiguous-wide run then matches frame for frame. Every other wide glyph the menu draws —
    // `▸`, `•`, `◂ Back`, the breadcrumb's `›` — goes through `put_text` on both sides and agrees.
    const Run amb_tall = drive(Cripple::None, /*ambiguous=*/1, kW, /*height=*/16);
    check(amb_tall.matched == steps,
          "…and with no marker to draw the same wide-ambiguous run agrees again, so the marker is the "
          "whole of the divergence" +
              (amb_tall.why.empty() ? "" : " — " + amb_tall.why));
  }

  // ---- 7. THE RELEASE POINT ------------------------------------------------------------------
  //
  // Both kinds are built and destroyed dozens of times above, so a host widget whose `destroy`
  // slot forgets something shows up here as a number rather than as nothing at all.
  {
    rolltui_shutdown();
    std::size_t live_bytes = 0, live_blocks = 0;
    rolltui_mem_stats(nullptr, nullptr, nullptr, &live_bytes, nullptr, &live_blocks);
    check(live_bytes == 0 && live_blocks == 0,
          "after every screen is freed the library holds nothing (" + std::to_string(live_bytes) +
              " bytes in " + std::to_string(live_blocks) + " blocks)");
  }

  return testkit::report("parity_test");
}
