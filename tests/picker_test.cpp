// rolltui/tests/picker_test.cpp — the column browser behind `filepicker`, driven directly.
//
// `picker_kind_test` is the consumer-shaped suite: a layout names the kind and a host makes the
// window calls. This one opts into the internal header to assert what a host cannot see and
// a text frame cannot show: the fade at the left edge, the per-folder memory, the anchor's
// slot, the dividers and their thumbs, the marks. It builds a tree of its own and reads the
// frame it draws.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <vector>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_widget_picker.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_preview.h"  /* INTERNAL: the formatters the head and the info say things with */
#include "rolltui/c/rolltui_screen.h"  /* INTERNAL: a frame of its own to draw into */
#include "rolltui_test.hpp"
#if !defined(_WIN32)
#include <signal.h>
#include <sys/types.h>
#endif
namespace {
namespace fs = std::filesystem;
void write_file(const fs::path& p, const std::string& t) {
  fs::create_directories(p.parent_path());
  const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) { (void)!::write(fd, t.data(), t.size()); ::close(fd); }
}
std::string text_of(RolltuiFrame* f) {
  RolltuiStr s{};
  rolltui_frame_to_text(f, &s);
  std::string out(s.p ? s.p : "", s.n);
  rolltui_str_free(&s);
  return out;
}
bool has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }
std::string str(const RolltuiStr& s) { return std::string(s.p ? s.p : "", s.n); }
std::vector<std::string> lines_of(const std::string& t) {
  std::vector<std::string> v;
  std::istringstream in(t);
  for (std::string l; std::getline(in, l);) v.push_back(l);
  return v;
}
}  // namespace
int main() {
  using testkit::check;
  const std::string tmp = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
  const fs::path root = fs::path(tmp) / ("rolltui_picker_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(root, ec);
  // a tree deep enough to clip, with a second entry to remember
  fs::create_directories(root / "alpha" / "nested" / "deeper");
  fs::create_directories(root / "beta");
  write_file(root / "alpha" / "one.txt", "1");
  write_file(root / "alpha" / "two.txt", "2");
  write_file(root / "alpha" / "nested" / "leaf.txt", "3");
  for (int i = 0; i < 12; ++i) write_file(root / "beta" / ("b" + std::to_string(i) + ".txt"), "b");
  write_file(root / "zeta.txt", "z");

  RolltuiContext* ctx = rolltui_context_new();
  rolltui_context_set_library_defaults(ctx);
  const RolltuiBindings* b = rolltui_bindings_default(ctx);
  const RolltuiPickerActions* A = rolltui_picker_default_actions();
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  {
    RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
    check(fx != nullptr, "the default-dark theme is built in");
    rolltui_effect_map_free(fx);
  }
  RolltuiScrollbarGlyphs glyphs{};
  rolltui_theme_scrollbar_glyphs(nullptr, &glyphs);  // the shipped set
  RolltuiPicker* p = rolltui_picker_new();
  auto key = [&](unsigned char k) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = k;
    return rolltui_picker_handle(p, &e, b, A);
  };
  auto draw = [&](int w, int h) {
    RolltuiFrame* f = rolltui_frame_new(w, h, styles[ROLLTUI_ROLE_BACKGROUND]);
    rolltui_picker_layout(p, RolltuiRect{0, 0, w, h});
    rolltui_picker_draw(p, f, styles, &glyphs, 0, 1);
    return f;
  };

  // ---- a deep start shows its ancestors, clipped at the left and FADED ----------------------
  const std::string deep = (root / "alpha" / "nested").string();
  rolltui_picker_go_to(p, deep.data(), deep.size());
  RolltuiStr got{};
  check(rolltui_picker_dir(p, &got) && str(got) == deep, "go_to lands the focus in the directory named [" + str(got) + "]");
  {
    RolltuiFrame* f = draw(60, 8);
    const std::string frame = text_of(f);
    check(has(frame, "nested") && has(frame, "leaf.txt") && has(frame, "deeper"),
          "the focused column and its preview are on screen, whole");
    check(rolltui_picker_faded_cells(p) > 0,
          "…and the ancestors clipped at the left edge are drawn faded, cell by cell (" + std::to_string(rolltui_picker_faded_cells(p)) + ")");
    rolltui_frame_free(f);
    f = draw(400, 8);
    check(rolltui_picker_faded_cells(p) == 0 && has(text_of(f), "alpha"), "…while a window that fits every column fades nothing — the control");
    rolltui_frame_free(f);
  }
  // ---- the memory: Left, then Right, is where you were; and the walk seeded it -------------
  key(ROLLTUI_KEY_LEFT);   // to alpha, cursor on nested (the walk's selection)
  key(ROLLTUI_KEY_LEFT);   // to root, cursor on alpha
  key(ROLLTUI_KEY_RIGHT);  // back into alpha
  check(rolltui_picker_dir(p, &got) && str(got) == (root / "alpha").string(), "Left twice and Right is back in alpha");
  int is_dir = 0;
  check(rolltui_picker_selected(p, &got, &is_dir) && str(got) == deep && is_dir == 1,
        "…on the entry the walk selected, since a deep start seeds the memory [" + str(got) + "]");
  key(ROLLTUI_KEY_DOWN);   // one.txt
  key(ROLLTUI_KEY_LEFT);
  key(ROLLTUI_KEY_DOWN);   // beta
  key(ROLLTUI_KEY_UP);     // alpha
  key(ROLLTUI_KEY_RIGHT);
  check(rolltui_picker_selected(p, &got, &is_dir) && str(got) == (root / "alpha" / "one.txt").string(),
        "a folder left and re-entered, after visiting a sibling, puts the cursor back where it was");
  // ---- THE POINTER: the entry a press would select is drawn hovered, and nothing moves ----------------------------------
  {
    // Where `name` is drawn: its first cell, found by the frame's own glyphs.
    auto find_cell = [](RolltuiFrame* f, const std::string& name, int& fx, int& fy) {
      for (int y = 0; y < rolltui_frame_height(f); ++y) {
        std::string row;
        std::vector<int> at;
        for (int x = 0; x < rolltui_frame_width(f); ++x) {
          RolltuiCellRaw c{};
          rolltui_frame_cell(f, x, y, &c);
          if (c.continuation) continue;
          at.push_back(static_cast<int>(row.size()));
          row.append(c.bytes, c.len);
        }
        const std::size_t k = row.find(name);
        if (k == std::string::npos) continue;
        for (std::size_t i = 0; i < at.size(); ++i)
          if (at[i] == static_cast<int>(k)) { fx = static_cast<int>(i); fy = y; return true; }
      }
      return false;
    };
    auto style_at = [](RolltuiFrame* f, int x, int y) {
      RolltuiCellRaw c{};
      rolltui_frame_cell(f, x, y, &c);
      return c.style;
    };
    auto same = [](const RolltuiStyle& a, const RolltuiStyle& b) { return std::memcmp(&a, &b, sizeof a) == 0; };
    RolltuiFrame* f = draw(80, 8);
    int bx = -1, by = -1, cx = -1, cy = -1;
    const bool found = find_cell(f, "beta", bx, by) && find_cell(f, "one.txt", cx, cy);
    rolltui_frame_free(f);
    check(found, "the root column shows beta, and the focused column the cursor's one.txt");
    rolltui_picker_set_hover(p, 1, bx + 1, by);
    f = draw(80, 8);
    check(same(style_at(f, bx, by), styles[ROLLTUI_ROLE_HOVER]) && same(style_at(f, cx, cy), styles[ROLLTUI_ROLE_MENU_SELECTED]),
          "the entry under the pointer is drawn in hover; the cursor's row keeps its own look");
    rolltui_frame_free(f);
    rolltui_picker_set_hover(p, 1, cx + 1, cy);
    f = draw(80, 8);
    check(same(style_at(f, cx, cy), styles[ROLLTUI_ROLE_MENU_SELECTED]), "…and stays the cursor's with the pointer on it");
    rolltui_frame_free(f);
    RolltuiEvent mv{};
    mv.kind = ROLLTUI_EVENT_MOUSE;
    mv.mouse.kind = RolltuiMouseEvent::Kind::Move;
    mv.mouse.x = bx + 1;
    mv.mouse.y = by;
    const int took = rolltui_picker_handle(p, &mv, b, A);
    check(!took && rolltui_picker_selected(p, &got, &is_dir) && str(got) == (root / "alpha" / "one.txt").string(),
          "a Move selects nothing: the cursor is where the keys left it");
    rolltui_picker_set_hover(p, 0, 0, 0);
    f = draw(80, 8);
    check(!same(style_at(f, bx, by), styles[ROLLTUI_ROLE_HOVER]), "with no pointer, no entry is hovered");
    rolltui_frame_free(f);
    // THE CURSOR ROW'S SHAPE: caps at the row's first and last cells, the name a cell narrower to leave them
    glyphs.cursor = ROLLTUI_CURSOR_CAPS;
    f = draw(80, 8);
    {
      std::size_t gn = 0;
      const char* left = rolltui_frame_glyph(f, cx - 1, cy, &gn);
      check(std::string(left, gn) == "\xE2\x96\x90" && same(style_at(f, cx, cy), styles[ROLLTUI_ROLE_MENU_SELECTED]),
            "caps: the cursor's row opens with a half block, its name still in the cursor's style");
    }
    rolltui_frame_free(f);
    glyphs.cursor = ROLLTUI_CURSOR_EDGES;
    f = draw(80, 8);
    check(style_at(f, cx, cy).bold && !same(style_at(f, cx, cy), styles[ROLLTUI_ROLE_MENU_SELECTED]),
          "edges: the name bold in the cursor's colour, on the row's own ground");
    rolltui_frame_free(f);
    glyphs.cursor = ROLLTUI_CURSOR_FILL;
  }
  // ---- Enter: a folder enters by default, takes with the option; a file always takes ------
  key(ROLLTUI_KEY_UP);     // nested
  key(ROLLTUI_KEY_ENTER);
  RolltuiPickerEvent ev{};
  check(!rolltui_picker_event(p, &ev) && rolltui_picker_dir(p, &got) && str(got) == deep, "Enter on a folder enters it by default, no event");
  key(ROLLTUI_KEY_ENTER);  // leaf.txt? the cursor in nested is on `deeper` (folders first)
  check(!rolltui_picker_event(p, &ev), "…and again on the folder inside");
  key(ROLLTUI_KEY_LEFT);
  key(ROLLTUI_KEY_DOWN);   // leaf.txt
  key(ROLLTUI_KEY_ENTER);
  check(rolltui_picker_event(p, &ev) && ev.kind == ROLLTUI_PICKER_EVENT_TAKEN && str(ev.path) == (root / "alpha" / "nested" / "leaf.txt").string(),
        "Enter on a file is a TAKEN event with its path [" + str(ev.path) + "]");
  check(!rolltui_picker_event(p, &ev), "…collected once");
  {
    RolltuiPickerOptions o{};
    rolltui_picker_options_init(&o);
    o.take_folders = 1;
    rolltui_picker_set_options(p, &o);
    key(ROLLTUI_KEY_UP);   // deeper
    key(ROLLTUI_KEY_ENTER);
    check(rolltui_picker_event(p, &ev) && ev.kind == ROLLTUI_PICKER_EVENT_TAKEN && str(ev.path) == (root / "alpha" / "nested" / "deeper").string(),
          "with take_folders, Enter on a folder is TAKEN too — a directory picker");
    rolltui_picker_options_init(&o);
    rolltui_picker_set_options(p, &o);
  }
  // ---- the dividers carry each column's thumb, and the marks are the cursor's and the trail's --
  {
    const std::string bdir = (root / "beta").string();
    rolltui_picker_go_to(p, bdir.data(), bdir.size());
    RolltuiFrame* f = draw(80, 6);  // four rows: beta's twelve entries scroll
    const std::string frame = text_of(f);
    std::size_t caps = 0;
    for (const char* g : {"\xE2\x94\x83", "\xE2\x95\xBB", "\xE2\x95\xB9", "\xE2\x80\xA2"}) { std::size_t at = 0; while ((at = frame.find(g, at)) != std::string::npos) { ++caps; at += 3; } }
    check(has(frame, "\xE2\x94\x82") && caps > 0, "a divider runs between the columns and the scrolled column's thumb sits on it (" + std::to_string(caps) + " capsule cells)");
    std::size_t marks = rolltui_frame_mark_count(f);
    check(marks >= 2, "the cursor's row and every trail row are marked (" + std::to_string(marks) + ")");
    int cursor_marks = 0, trail_marks = 0;
    for (std::size_t i = 0; i < marks; ++i) {
      int x, y, cells, state; unsigned long long since; double fr;
      rolltui_frame_mark_at(f, i, &x, &y, &cells, &state, &since, &fr);
      if (state == ROLLTUI_EFFECT_STATE_PICKER_CURSOR) ++cursor_marks;
      if (state == ROLLTUI_EFFECT_STATE_PICKER_TRAIL) ++trail_marks;
    }
    check(cursor_marks == 1 && trail_marks >= 1, "…one cursor mark, and a trail mark per ancestor (" + std::to_string(cursor_marks) + "/" + std::to_string(trail_marks) + ")");
    rolltui_frame_free(f);
    RolltuiPickerOptions o{};
    rolltui_picker_options_init(&o);
    o.dividers = 0;
    rolltui_picker_set_options(p, &o);
    f = draw(80, 6);
    check(!has(text_of(f), "\xE2\x94\x82"), "…and with dividers off there is none");
    rolltui_frame_free(f);
  }
  // ---- type to jump: a key lands on the next name starting with it; a dotfile counts by the
  // letter after its dot, and the dot itself cycles the dotfiles -------------------------------
  {
    // Two names for the rule, added for this block and removed after it so the counts elsewhere
    // stay what they were: a dotfile, and a second name starting with a.
    write_file(root / ".hidden", "h");
    write_file(root / "avocado.txt", "a");
    const std::string top = root.string();
    rolltui_picker_go_to(p, top.data(), top.size());
    auto typed = [&](RolltuiCodepoint ch, bool shift) {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_KEY;
      e.key.key = ROLLTUI_KEY_CHAR;
      e.key.ch = ch;
      e.key.shift = shift ? 1 : 0;
      rolltui_picker_handle(p, &e, b, A);
      RolltuiStr path{};
      int is_dir = 0;
      rolltui_picker_selected(p, &path, &is_dir);
      const std::string s(path.p ? path.p : "", path.n);
      rolltui_str_free(&path);
      return s.substr(s.rfind('/') + 1);
    };
    check(typed('z', false) == "zeta.txt", "z lands on zeta.txt");
    check(typed('h', false) == ".hidden", "h reaches .hidden by the letter after its dot");
    check(typed('.', false) == ".hidden", "…and the dot itself cycles the dotfiles");
    check(typed('a', false) == "alpha" && typed('a', false) != "alpha", "a: alpha, then the next a — the cursor moves on when already on one");
    check(typed('A', false) == "alpha", "Shift-A goes back");
    check(typed('q', false) == "alpha", "a key no name starts with moves nothing");
    // LETTERS TYPED IN QUICK SUCCESSION are a name's start: "av" is avocado.txt, past alpha; slowly, each starts again
    rolltui_picker_set_now(p, 5000);
    typed('a', false);
    rolltui_picker_set_now(p, 5200);
    check(typed('v', false) == "avocado.txt", "a then v within a second: the name starting av");
    rolltui_picker_set_now(p, 9000);
    check(typed('z', false) == "zeta.txt", "…and a letter typed later starts again: z");
    rolltui_picker_set_now(p, 0);
    fs::remove(root / ".hidden");
    fs::remove(root / "avocado.txt");
    rolltui_picker_go_to(p, top.data(), top.size());
  }
  // ---- a press on empty space chooses nothing, so it moves nothing ----------------------------
  {
    const std::string deep = (root / "alpha").string();
    rolltui_picker_go_to(p, deep.data(), deep.size());
    rolltui_frame_free(draw(120, 20));  // laid out: the columns have places
    RolltuiPickerStatus before{};
    rolltui_picker_status(p, &before);
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_MOUSE;
    e.mouse.kind = RolltuiMouseEvent::Kind::Press;
    e.mouse.button = 1;
    e.mouse.x = 2;   // the first column, well inside it
    e.mouse.y = 15;  // far below its few entries
    rolltui_picker_handle(p, &e, b, A);
    RolltuiPickerStatus after{};
    rolltui_picker_status(p, &after);
    check(after.column == before.column && after.columns == before.columns,
          "a press on the empty space below a column's entries leaves the focus and the columns where they were [" +
              std::to_string(after.column) + "/" + std::to_string(after.columns) + "]");
    e.mouse.y = 0;  // the head row of that first column: the folder's name
    rolltui_picker_handle(p, &e, b, A);
    rolltui_picker_status_release(&after);
    rolltui_picker_status(p, &after);
    check(after.column <= before.column && after.columns == after.column + 1, "…while a press on a head row focuses that folder (the one under the press), with its preview and nothing deeper [" + std::to_string(after.column) + "/" + std::to_string(after.columns) + "]");
    rolltui_picker_status_release(&before);
    rolltui_picker_status_release(&after);
  }
  // ---- only the left button chooses: a right, middle, back or forward press is the host's ------
  {
    const std::string deep = (root / "alpha").string();
    rolltui_picker_go_to(p, deep.data(), deep.size());
    rolltui_frame_free(draw(120, 20));
    auto selected = [&]() {
      RolltuiStr s{};
      int d = 0;
      rolltui_picker_selected(p, &s, &d);
      const std::string r = str(s);
      rolltui_str_free(&s);
      return r;
    };
    auto mouse = [&](RolltuiMouseEvent::Kind kind, int button, int y) {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_MOUSE;
      e.mouse.kind = kind;
      e.mouse.button = button;
      e.mouse.x = 2;
      e.mouse.y = y;
      return rolltui_picker_handle(p, &e, b, A) != 0;
    };
    const std::string was = selected();
    bool moved = false, claimed = false, took = false;
    for (int button : {2, 3, 8, 9})
      for (int y = 1; y <= 6; ++y)
        for (auto kind : {RolltuiMouseEvent::Kind::Press, RolltuiMouseEvent::Kind::DoubleClick}) {
          claimed = mouse(kind, button, y) || claimed;
          moved = moved || selected() != was;
          took = rolltui_picker_event(p, &ev) || took;
        }
    check(!moved && !took && !claimed,
          "a press or double-click with any button but the left selects nothing, opens nothing, and is left to the host");
    bool left_moved = false;
    for (int y = 1; y <= 6 && !left_moved; ++y) {
      mouse(RolltuiMouseEvent::Kind::Press, 1, y);
      left_moved = selected() != was;
    }
    check(left_moved, "CONTROL: the left button on the same rows does select");
    rolltui_picker_go_to(p, deep.data(), deep.size());
  }
  // ---- focusing a column by index: a breadcrumb's segment ------------------------------------
  {
    const std::string deep = (root / "alpha").string();
    rolltui_picker_go_to(p, deep.data(), deep.size());
    RolltuiPickerStatus st{};
    rolltui_picker_status(p, &st);
    const std::size_t was = st.column;
    rolltui_picker_status_release(&st);
    rolltui_picker_focus_column(p, 1);
    rolltui_picker_status(p, &st);
    RolltuiStr d{};
    rolltui_picker_dir(p, &d);
    check(was > 2 && st.column == 2 && std::string(d.p ? d.p : "", d.n) == "/" && st.columns == 3,
          "focus_column(1) puts the focus on the root's listing and, as Left does, keeps only its preview [" + std::to_string(st.column) + "/" + std::to_string(st.columns) + "]");
    // THE TOP COLUMN: one entry, the root itself, so the root can be chosen like any folder.
    rolltui_picker_focus_column(p, 0);
    rolltui_picker_status_release(&st);
    rolltui_picker_status(p, &st);
    rolltui_str_free(&d);
    {
      RolltuiStr sel{};
      int is_dir = 0;
      rolltui_picker_selected(p, &sel, &is_dir);
      check(st.column == 1 && st.entries == 1 && std::string(sel.p ? sel.p : "", sel.n) == "/" && is_dir,
            "…and column 0 is the top, holding the root as its one entry, selected [" + std::string(sel.p ? sel.p : "", sel.n) + "]");
      rolltui_str_free(&sel);
    }
    rolltui_picker_dir(p, &d);
    rolltui_str_free(&d);
    rolltui_picker_status_release(&st);
    rolltui_picker_focus_column(p, 999);
    rolltui_picker_status(p, &st);
    check(st.column == st.columns, "…and past the last column lands on the last");
    rolltui_picker_status_release(&st);
  }
  // ---- a size column when sorting by size, a modified column on request ----------------------
  {
    write_file(root / "beta" / "big.bin", std::string(2048, 'x'));
    const std::string bdir = (root / "beta").string();
    RolltuiPickerOptions o{};
    rolltui_picker_options_init(&o);
    o.sort = ROLLTUI_SORT_SIZE;
    rolltui_picker_set_options(p, &o);
    rolltui_picker_go_to(p, bdir.data(), bdir.size());
    RolltuiFrame* f = draw(80, 20);
    const std::string frame = text_of(f);
    check(has(frame, "big.bin") && has(frame, "2.0K"), "sorted by size, the sizes show beside the names without being asked for [2.0K]");
    rolltui_frame_free(f);
    rolltui_picker_options_init(&o);
    rolltui_picker_set_options(p, &o);
    f = draw(80, 20);
    check(!has(text_of(f), "2.0K"), "…and sorted by name they are hidden again");
    rolltui_frame_free(f);
    o.sort = ROLLTUI_SORT_SIZE;
    o.show_size = ROLLTUI_SHOW_NEVER;
    rolltui_picker_set_options(p, &o);
    f = draw(80, 20);
    check(!has(text_of(f), "2.0K"), "NEVER is a person's own word: sorted by size with sizes never shown, none show");
    rolltui_frame_free(f);
    o.show_size = ROLLTUI_SHOW_ALWAYS;
    o.sort = ROLLTUI_SORT_NAME;
    rolltui_picker_set_options(p, &o);
    f = draw(80, 20);
    check(has(text_of(f), "2.0K"), "…and ALWAYS shows them under any sort");
    rolltui_frame_free(f);
    rolltui_picker_options_init(&o);
    o.sort = ROLLTUI_SORT_SIZE;
    o.reversed = 1;
    rolltui_picker_set_options(p, &o);
    f = draw(80, 20);
    {
      const std::string t = text_of(f);
      check(t.find("big.bin") != std::string::npos && t.find("big.bin") > t.find("beta"), "reversed, the largest file comes last");
    }
    rolltui_frame_free(f);
    rolltui_picker_options_init(&o);
    o.show_modified = ROLLTUI_SHOW_ALWAYS;
    rolltui_picker_set_options(p, &o);
    f = draw(80, 20);
    {
      char today[16];
      time_t now = time(nullptr);
      struct tm tm_now;
      localtime_r(&now, &tm_now);
      strftime(today, sizeof today, "%b", &tm_now);
      check(has(text_of(f), std::string(today)), "asked for, the modified column shows the month a file just written carries [" + std::string(today) + "]");
    }
    rolltui_frame_free(f);
    rolltui_picker_options_init(&o);
    rolltui_picker_set_options(p, &o);
  }
  // ---- the status line's facts ---------------------------------------------------------------
  {
    RolltuiPickerStatus st{};
    rolltui_picker_status(p, &st);
    check(st.entries == 13 && st.columns >= 2 && st.column == st.columns && st.error.n == 0,
          "the status says how many entries, which column of how many, and no error (" + std::to_string(st.entries) + ", " + std::to_string(st.column) + "/" + std::to_string(st.columns) + ")");
    rolltui_picker_status_release(&st);
    const std::string nosuch = (root / "nowhere").string();
    rolltui_picker_go_to(p, nosuch.data(), nosuch.size());
    rolltui_picker_status(p, &st);
    RolltuiFrame* f = draw(80, 6);
    check(st.error.n == 0 && has(text_of(f), "cannot"), "a start that cannot be entered is a column that says why, on screen");
    rolltui_frame_free(f);
    rolltui_picker_status_release(&st);
  }
  // ---- THE FILE PREVIEW: what a file holds, in the right half, when a person asked for it ----------
  {
    const fs::path pv = root / "preview";
    write_file(pv / "notes.txt", std::string("hello world\n\tindented\n") + std::string(300, 'x') + "\n\x1b[31mred\x1b[0m escape\nlast line\n");
    {
      std::string bin;
      for (int i = 0; i < 3000; ++i) bin.push_back(static_cast<char>(i % 251 == 0 ? 0 : (i * 7 + 3) & 0xFF));
      bin[0] = 0;  // a NUL in the first block is what makes it binary
      write_file(pv / "blob.bin", bin);
    }
    write_file(pv / "empty", "");
    write_file(pv / "bad.txt", std::string("caf\xC3\xA9 ok \xFF\xFE broken\n"));
    {
      std::string big;
      for (int i = 0; i < 6000; ++i) big += "line number " + std::to_string(i) + " of a file bigger than the limit\n";
      write_file(pv / "big.txt", big);
    }
    write_file(pv / "sub" / "inner.txt", "inner\n");
    ::mkfifo((pv / "pipe").c_str(), 0644);
    write_file(pv / "secret.txt", "nobody reads this\n");
    ::chmod((pv / "secret.txt").c_str(), 0);

    auto go_file = [&](const char* name) {
      const std::string path = (pv / name).string();
      rolltui_picker_go_to(p, path.data(), path.size());
    };
    auto frame_of = [&](int w, int h) {
      RolltuiFrame* f = draw(w, h);
      const std::string t = text_of(f);
      rolltui_frame_free(f);
      return t;
    };
    RolltuiPickerOptions po{};
    rolltui_picker_options_init(&po);

    // OFF is what a browser always did: nothing to the right of a file.
    go_file("notes.txt");
    check(!has(frame_of(110, 14), "hello world"), "with the preview off, a file shows nothing to its right — the control");

    po.preview = ROLLTUI_PREVIEW_RIGHT;
    rolltui_picker_set_options(p, &po);
    {
      const std::string t = frame_of(110, 14);
      check(has(t, "hello world") && has(t, "indented"), "with it on, the file's lines are drawn beside the cursor");
      check(has(t, "text \xC2\xB7 5 lines"), "…the head says what it is and how long [text · 5 lines]");
      check(!has(t, "\t") && has(t, "    indented"), "…a tab is spaces, to a stop of four");
      check(has(t, "\xE2\x80\xA6"), "…a line longer than the pane ends at an ellipsis");
      check(has(t, "\xE2\x90\x9B[31mred"), "…an escape is its picture (␛), never itself");
      check(!has(t, "\x1b"), "…and not one raw ESC reaches the frame");
    }
    // A FOLDER under the cursor is what it always was, in the same half.
    {
      go_file("notes.txt");
      key(ROLLTUI_KEY_HOME);  // folders sort first: the cursor is on `sub`
      const std::string t = frame_of(110, 14);
      check(!has(t, "text \xC2\xB7") && !has(t, "binary \xC2\xB7") && has(t, "inner.txt"),
            "a FOLDER under the cursor is its listing, as always — no file preview, in the same half");
    }
    // BINARY IS OFFSETS, BYTES AND A GUTTER — and the byte count per row fits the pane.
    go_file("blob.bin");
    {
      const std::string wide = frame_of(200, 14);
      check(has(wide, "binary \xC2\xB7") && has(wide, "0000  00 ") && has(wide, "|"), "a file with a NUL in it is a hex dump [offset, byte pairs, |gutter|]");
      check(has(wide, "0010  ") && !has(wide, "0008  "), "…sixteen bytes to a row when the pane has the room");
      const std::string narrow = frame_of(80, 14);
      check(has(narrow, "0008  ") && !has(narrow, "0010  0"), "…and a narrower pane shows fewer bytes to a row, so its second row starts at 8");
    }
    go_file("empty");
    check(has(frame_of(110, 10), "(empty file)"), "an empty file says so");
    go_file("bad.txt");
    check(has(frame_of(110, 10), "caf\xC3\xA9 ok \xEF\xBF\xBD\xEF\xBF\xBD broken"), "text with bytes that are not UTF-8 draws them as � and keeps going");
    go_file("pipe");
    check(has(frame_of(110, 10), "not a regular file"), "a FIFO is never read — opening it must not hang the browser");
    if (::geteuid() != 0) {
      go_file("secret.txt");
      check(has(frame_of(110, 10), "Permission denied"), "a file that cannot be read says why, in words");
    }
    // A FILE BIGGER THAN THE LIMIT shows its first 256 KB and says so; nothing is lazy.
    go_file("big.txt");
    {
      const std::string t = frame_of(110, 12);
      check(has(t, "first 256K of "), "a file over 256 KB says how much of it is shown [head]");
      check(has(t, "line number 0 of a file"), "…and starts at its first line");
    }
    // MARKDOWN IS RENDERED, NOT LISTED: headings, a table with its borders, a code box, a diagram drawn as a diagram
    write_file(pv / "doc.md", "# Title\n\nSome **bold** text.\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```mermaid\ngraph TD\n  A[Start] --> B[End]\n```\n");
    write_file(pv / "flow.mmd", "graph LR\n  X[one] --> Y[two]\n");
    go_file("doc.md");
    {
      const std::string t = frame_of(110, 40);
      check(has(t, "markdown \xC2\xB7") && has(t, "# Title") && has(t, "Some bold text."), "a Markdown file is rendered: its heading, its emphasis without the asterisks");
      check(has(t, "\xE2\x94\x82 a") && has(t, "\xE2\x94\x9C") && !has(t, "|---|"), "…its table has borders and no pipes");
      check(has(t, "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90") && has(t, "Start") && has(t, "\xE2\x96\xBC") && !has(t, "graph TD"), "…and a mermaid block is the diagram, not its source");
    }
    go_file("flow.mmd");
    {
      const std::string t = frame_of(110, 12);
      check(has(t, "diagram \xC2\xB7") && has(t, "one") && has(t, "\xE2\x96\xB6") && !has(t, "graph LR"), "a .mmd file is its diagram [head says so]");
    }
    // narrower than the diagram: its source, and why
    go_file("doc.md");
    {
      const std::string t = frame_of(60, 40);
      check(has(t, "# Title"), "in a narrow pane the Markdown is laid out for the width it has");
    }
    // THE KEYS ENTER THE PREVIEW with Right on a file; Up/Down scroll it, Left returns.
    go_file("notes.txt");
    {
      RolltuiStr sel{};
      int dir = 0;
      (void)frame_of(110, 4);
      check(!rolltui_picker_preview_focused(p), "the keys start in the list");
      key(ROLLTUI_KEY_RIGHT);
      check(rolltui_picker_preview_focused(p), "Right on a file, with the preview on, puts the keys in it");
      key(ROLLTUI_KEY_DOWN);
      key(ROLLTUI_KEY_DOWN);
      std::string t = frame_of(110, 4);
      check(!has(t, "hello world") && has(t, "\xE2\x90\x9B[31mred"), "…Down scrolls the FILE two lines");
      rolltui_picker_selected(p, &sel, &dir);
      check(str(sel) == (pv / "notes.txt").string(), "…and the cursor in the list did not move");
      key(ROLLTUI_KEY_HOME);
      t = frame_of(110, 4);
      check(has(t, "hello world"), "Home is the top of the file");
      key(ROLLTUI_KEY_END);
      t = frame_of(110, 4);
      check(has(t, "last line") && !has(t, "hello world"), "End is the bottom");
      // Right AGAIN gives the preview the whole picker; Left steps back to the half, and again to the list
      {
        RolltuiPickerStatus st{};
        key(ROLLTUI_KEY_RIGHT);
        t = frame_of(110, 4);
        rolltui_picker_status(p, &st);
        check(st.preview == 3 && has(t, "text \xC2\xB7 5 lines") && t.find("notes.txt") < 3 && !has(t.substr(0, t.find('\n')), "\xE2\x94\x82"),
              "Right again: the preview has the whole picker, and the status says so");
        key(ROLLTUI_KEY_RIGHT);
        rolltui_picker_status(p, &st);
        check(st.preview == 3, "…and Right once more is nothing further");
        key(ROLLTUI_KEY_DOWN);
        t = frame_of(110, 4);
        check(!has(t, "hello world"), "…the keys still scroll the file");
        key(ROLLTUI_KEY_LEFT);
        rolltui_picker_status(p, &st);
        check(st.preview == 2 && rolltui_picker_preview_focused(p), "Left steps back to the half, the keys still in the file");
        key(ROLLTUI_KEY_RIGHT);
        key(ROLLTUI_KEY_ESCAPE);
        rolltui_picker_status(p, &st);
        check(st.preview == 2, "Escape steps back one place as well: from the whole picker to the half");
        key(ROLLTUI_KEY_LEFT);
        rolltui_picker_status(p, &st);
        check(st.preview == 1 && !rolltui_picker_preview_focused(p), "…and Left again is the list");
        key(ROLLTUI_KEY_RIGHT);
      }
      rolltui_picker_event(p, &ev);
      key(ROLLTUI_KEY_ESCAPE);
      check(!rolltui_picker_preview_focused(p) && !rolltui_picker_event(p, &ev), "Escape goes back to the list and does NOT leave the picker");
      key(ROLLTUI_KEY_RIGHT);
      key(ROLLTUI_KEY_LEFT);
      check(!rolltui_picker_preview_focused(p), "Left goes back to the list too");
      key(ROLLTUI_KEY_RIGHT);
      key(ROLLTUI_KEY_ENTER);
      check(rolltui_picker_event(p, &ev) && ev.kind == ROLLTUI_PICKER_EVENT_TAKEN, "Enter in the preview still takes the file");
      // the wheel scrolls it wherever the keys are
      key(ROLLTUI_KEY_LEFT);
      key(ROLLTUI_KEY_HOME);
      (void)frame_of(110, 4);
      {
        RolltuiEvent w{};
        w.kind = ROLLTUI_EVENT_MOUSE;
        w.mouse.kind = RolltuiMouseEvent::Kind::WheelDown;
        w.mouse.x = 100;
        w.mouse.y = 3;
        rolltui_picker_handle(p, &w, b, A);
        t = frame_of(110, 4);
        check(!has(t, "hello world"), "the wheel over the preview scrolls it, with the keys in the list");
      }
      rolltui_str_free(&sel);
    }
    // THE WINDOW'S BAR is the file's while a file is shown.
    {
      RolltuiScrollExtent ex{};
      go_file("big.txt");
      (void)frame_of(110, 12);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.total > 1000 && ex.visible > 0 && ex.first == 0,
            "the window's scrollbar reports the FILE's rows [" + std::to_string(ex.total) + "]");
      rolltui_picker_scroll_to(p, 500);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first == 500, "…and dragging it scrolls the file");
      // the last row of a truncated file is the note that it stops there: reachable, not squeezed out by the clamp
      check(!has(frame_of(110, 12), "first 256 KB of "), "a truncated file's note is at its end, not at the top");
      rolltui_picker_scroll_to(p, 100000000);
      const std::string end = frame_of(110, 12);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total && has(end, "first 256 KB of "),
            "…and scrolled to the end the last row says the file goes on");
    }
    // A RESIZE IS THE NEXT FRAME: the hex view works out again how many bytes fit a row and keeps the same BYTE at the top,
    // leaves no blank rows below the end when the window grows taller, and fills a window taller than any it was tested in;
    // a Markdown view keeps the same words at the top when the width lays it out again
    {
      RolltuiScrollExtent ex{};
      go_file("blob.bin");
      (void)frame_of(200, 14);  // sixteen bytes to a row
      rolltui_picker_scroll_to(p, 20);
      std::string t = frame_of(200, 14);
      check(has(t, "0140  ") && !has(t, "0130  "), "hex: scrolled twenty rows down at sixteen bytes to a row, the top is offset 0x140");
      t = frame_of(80, 14);
      check(rolltui_picker_scroll_extent(p, &ex) && (ex.first == 40 || ex.first == 80) && has(t, "0140  ") && !has(t, "0138  "),
            "…narrowed to fewer bytes to a row, the same byte is at the top (0x140 is row 40 at eight to a row, 80 at four) [row " + std::to_string(ex.first) + "]");
      t = frame_of(200, 14);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first == 20 && has(t, "0140  "), "…and widened again, the same byte still (row 20)");
      // taller: the end is not left with blank rows under it
      (void)frame_of(200, 8);
      rolltui_picker_scroll_to(p, 100000);
      (void)frame_of(200, 8);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total, "hex: scrolled to the end, the last row is the bottom one");
      t = frame_of(200, 30);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total && has(t, "0bb0  "),
            "…and in a taller window the top comes back up so the end is still the bottom [first " + std::to_string(ex.first) + " of " + std::to_string(ex.total) + "]");
      // a window taller than sixty-four rows is filled
      rolltui_picker_scroll_to(p, 0);
      t = frame_of(200, 90);
      check(has(t, "0580  "), "hex: a ninety-row window shows all eighty-nine rows it has room for, not the first sixty-four");
      // shorter again: nothing to re-clamp, the same top
      t = frame_of(200, 10);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first == 0 && ex.visible == 9, "…and shorter again, the same top and the new height");
      // no room at all is still a frame
      t = frame_of(30, 6);
      check(has(t, "|"), "hex: in a pane too narrow for eight bytes it is fewer to a row, not nothing");
      // AT THE END, THE END STAYS: a shorter window, a narrower one, a taller one — the last row is the bottom one each time
      (void)frame_of(200, 14);
      rolltui_picker_scroll_to(p, 100000);
      (void)frame_of(200, 14);
      t = frame_of(200, 6);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total && has(t, "0bb0  "),
            "hex: at the end, a SHORTER window keeps the end at the bottom (the top moves down) [first " + std::to_string(ex.first) + " of " + std::to_string(ex.total) + "]");
      t = frame_of(80, 6);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total, "…a narrower pane, with fewer bytes to a row, too");
      t = frame_of(200, 14);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total && has(t, "0bb0  "), "…and back again");
      // NEITHER CAN BE PINNED when the whole file fits: it sits at its top, and a window that shrinks again leaves it there
      t = frame_of(200, 200);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first == 0 && ex.visible >= ex.total && has(t, "0000  "),
            "hex: in a window that holds the whole file it starts at the top");
      t = frame_of(200, 14);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first == 0 && has(t, "0000  "), "…and shrunk again it is still at the top, not thrown to the end");
    }
    {
      std::string doc;
      for (int i = 0; i < 60; ++i)
        doc += "Paragraph " + std::to_string(i) + ": the quick brown fox jumps over the lazy dog and keeps running past the edge of any pane that is narrow enough to make it wrap onto a second and a third line.\n\n";
      write_file(pv / "long.md", doc);
      go_file("long.md");
      (void)frame_of(160, 20);
      // scroll until Paragraph 30 is at the top of the wide layout
      RolltuiScrollExtent ex{};
      std::string t;
      for (std::size_t at = 0; at < 400; ++at) {
        rolltui_picker_scroll_to(p, at);
        t = frame_of(160, 20);
        std::istringstream in(t);
        std::string l0, l1;
        std::getline(in, l0);
        std::getline(in, l1);
        if (has(l1, "Paragraph 30:")) break;
      }
      auto top_row = [&](const std::string& frame) {
        std::istringstream in(frame);
        std::string l0, l1;
        std::getline(in, l0);
        std::getline(in, l1);
        return l1;
      };
      check(has(top_row(t), "Paragraph 30:"), "markdown: Paragraph 30 is at the top in the wide layout");
      t = frame_of(90, 20);
      check(has(top_row(t), "Paragraph 30:"), "…narrowed until every paragraph wraps twice more, it is still what is at the top [" + top_row(t).substr(0, 60) + "]");
      t = frame_of(160, 20);
      check(has(top_row(t), "Paragraph 30:"), "…and widened again");
      // AT THE END the end stays: laid out again at another width, or in a shorter window, the last paragraph is still the bottom
      rolltui_picker_scroll_to(p, 100000);
      t = frame_of(160, 20);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total && has(t, "Paragraph 59:"),
            "markdown: scrolled to the end, the last paragraph is on screen");
      t = frame_of(90, 20);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total && has(t, "Paragraph 59:") && !has(top_row(t), "Paragraph 30:"),
            "…narrowed, the document is laid out again and the end is still the bottom, not the old words at the top");
      t = frame_of(90, 12);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first + ex.visible == ex.total && has(t, "Paragraph 59:"), "…and in a shorter window too");
      // NEITHER CAN BE PINNED when the whole document fits: the top, and it stays the top when the window shrinks again
      t = frame_of(160, 300);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first == 0 && ex.visible >= ex.total && has(top_row(t), "Paragraph 0:"),
            "markdown: in a window that holds the whole document it starts at the top");
      t = frame_of(160, 20);
      check(rolltui_picker_scroll_extent(p, &ex) && ex.first == 0 && has(top_row(t), "Paragraph 0:"), "…and shrunk again it is still at the top");
    }
    // THE START STAYS THE START: a document that opens on a diagram (a picture has no text to anchor to) keeps the diagram at the
    // top when the width changes, instead of jumping to the first text under it
    {
      std::string doc = "```mermaid\ngraph TD\n  A[Start] --> B[End]\n```\n\n";
      for (int i = 0; i < 30; ++i) doc += "Paragraph " + std::to_string(i) + " of the text under the picture.\n\n";
      write_file(pv / "pic.md", doc);
      go_file("pic.md");
      (void)frame_of(110, 14);
      std::string t = frame_of(110, 14);
      check(has(t, "Start"), "a document that opens on a diagram shows it");
      t = frame_of(70, 14);
      check(has(t, "Start") && has(t, "\xE2\x94\x8C"), "…still at its top when the width lays it out again");
    }
    // NOTHING IS LEFT BEHIND by looking at files: a picker that previewed text, a diagram, hex and an error is freed whole
    {
      std::size_t before = 0, after = 0;
      rolltui_mem_stats(nullptr, nullptr, nullptr, &before, nullptr, nullptr);
      RolltuiPicker* q = rolltui_picker_new();
      rolltui_picker_set_options(q, &po);
      for (const char* name : {"notes.txt", "blob.bin", "doc.md", "flow.mmd", "empty", "pipe", "big.txt", "sub"}) {
        const std::string path = (pv / name).string();
        rolltui_picker_go_to(q, path.data(), path.size());
        RolltuiFrame* fr = rolltui_frame_new(110, 30, styles[ROLLTUI_ROLE_BACKGROUND]);
        rolltui_picker_layout(q, RolltuiRect{0, 0, 110, 30});
        rolltui_picker_draw(q, fr, styles, &glyphs, 0, 1);
        rolltui_frame_free(fr);
      }
      rolltui_picker_free(q);
      rolltui_mem_stats(nullptr, nullptr, nullptr, &after, nullptr, nullptr);
      check(after == before, "a picker that previewed a text file, a diagram, a dump, a FIFO and a folder frees all of it (" + std::to_string(after) + " vs " + std::to_string(before) + ")");
    }
    ::chmod((pv / "secret.txt").c_str(), 0644);
    rolltui_picker_options_init(&po);
    rolltui_picker_set_options(p, &po);
  }
  // ---- WHAT THE CURSOR IS ON, SAID WITHOUT ASKING: `info` ----------------------------------------------------------------------
  // A second row in the title band; at the head of whatever is right of the cursor, the entry's mode and when it was written. A folder's
  // own over the column that lists it, a file's under its preview's head or, with no preview, in a small pane of its own.
  {
    const char* had_tz = std::getenv("TZ");
    const std::string kept_tz = had_tz ? had_tz : "";
    ::setenv("TZ", "UTC", 1);  // the times are said in local time, so the frame is the same on every machine
    ::tzset();
    const fs::path in = root / "info";
    fs::create_directories(in / "dir");
    fs::create_directories(in / "many");
    write_file(in / "dir" / "note.txt", std::string(1234, 'n'));
    write_file(in / "plain.txt", "twelve bytes");
    for (int i = 0; i < 10; ++i) write_file(in / "many" / ("f" + std::to_string(i) + ".txt"), "x");
    auto stamp = [](const fs::path& f, int y, int mo, int d, int h, int mi, mode_t mode) {
      std::tm tm{};
      tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi;
      const time_t t = timegm(&tm);
      const struct timespec ts[2] = {{t, 0}, {t, 0}};
      ::utimensat(AT_FDCWD, f.c_str(), ts, 0);
      ::chmod(f.c_str(), mode);
    };
    stamp(in / "dir" / "note.txt", 2026, 3, 14, 9, 26, 0640);
    stamp(in / "plain.txt", 2025, 12, 31, 23, 59, 0600);
    stamp(in / "many", 2026, 1, 2, 3, 4, 0755);
    stamp(in / "dir", 2026, 6, 1, 12, 34, 0750);

    // THE FORMATTERS, one way wherever these are said
    {
      char m[11];
      rolltui_fileinfo_mode_text(040755, m);
      check(std::string(m) == "drwxr-xr-x", "a folder's mode reads drwxr-xr-x [" + std::string(m) + "]");
      rolltui_fileinfo_mode_text(0100644, m);
      check(std::string(m) == "-rw-r--r--", "a file's reads -rw-r--r-- [" + std::string(m) + "]");
      rolltui_fileinfo_mode_text(0120777, m);
      check(std::string(m) == "lrwxrwxrwx", "a link's starts with l [" + std::string(m) + "]");
      rolltui_fileinfo_mode_text(0100751, m);
      check(std::string(m) == "-rwxr-x--x", "each triplet is its own bits, none run into the next [" + std::string(m) + "]");
      std::tm tm{};
      tm.tm_year = 2026 - 1900; tm.tm_mon = 2; tm.tm_mday = 14; tm.tm_hour = 9; tm.tm_min = 26;
      const long long t = (long long)timegm(&tm);
      char w[32];
      rolltui_fileinfo_when_text(t, 0, w, sizeof w);
      check(std::string(w) == "2026-03-14 09:26", "the time reads to the minute [" + std::string(w) + "]");
      rolltui_fileinfo_when_text(t, 1, w, sizeof w);
      check(std::string(w) == "2026-03-14", "…or the date alone [" + std::string(w) + "]");
      rolltui_fileinfo_when_text(0, 0, w, sizeof w);
      check(std::string(w) == "-", "a time that is not known is a dash [" + std::string(w) + "]");
      rolltui_fileinfo_when_text(t, 0, w, 5);
      check(std::string(w) == "-" || std::strlen(w) < 5, "a buffer too small for it is never overrun [" + std::string(w) + "]");
      char z[24];
      const struct { long long n; const char* want; } sizes[] = {{0, "0 B"}, {12, "12 B"}, {1023, "1023 B"}, {1024, "1.0K"}, {1234, "1.2K"},
                                                                  {10240, "10K"}, {2097152, "2.0M"}, {5368709120LL, "5.0G"}};
      for (const auto& c : sizes) {
        rolltui_fileinfo_size_text(c.n, z, sizeof z);
        check(std::string(z) == c.want, "size " + std::to_string(c.n) + " reads " + c.want + " [" + z + "]");
      }
    }

    auto on = [&](unsigned char preview, unsigned char show_size) {
      RolltuiPickerOptions o{};
      rolltui_picker_options_init(&o);
      o.info = 1;
      o.preview = preview;
      o.show_size = show_size;
      rolltui_picker_set_options(p, &o);
    };
    auto plain_options = [&]() {
      RolltuiPickerOptions o{};
      rolltui_picker_options_init(&o);
      rolltui_picker_set_options(p, &o);
    };
    const std::string dir_p = in.string(), file_p = (in / "plain.txt").string(), many_p = (in / "many").string();

    // OFF BY DEFAULT: a host that never heard of it has the screen it always had
    plain_options();
    rolltui_picker_go_to(p, dir_p.data(), dir_p.size());
    {
      RolltuiFrame* f = draw(100, 12);
      const std::string t = text_of(f);
      check(!has(t, "drwxr-x---") && has(lines_of(t)[1], "note.txt"), "with `info` off nothing is added and the list starts on the row under the heads");
      rolltui_frame_free(f);
    }

    // A FOLDER UNDER THE CURSOR: its own mode and time, in the second row of the band, over the column that lists it
    on(ROLLTUI_PREVIEW_OFF, ROLLTUI_SHOW_WITH_SORT);
    rolltui_picker_go_to(p, dir_p.data(), dir_p.size());
    {
      RolltuiFrame* f = draw(100, 12);
      const auto L = lines_of(text_of(f));
      check(has(L[0], "dir") && has(L[0], "1 entry"), "the column's head is as it was [" + L[0] + "]");
      check(has(L[1], "drwxr-x--- 2026-06-01 12:34"), "on a folder, the row under the head says the folder's mode and time [" + L[1] + "]");
      check(has(L[2], "note.txt") && !has(L[1], "note.txt"), "…and the list starts one row lower, under it");
      rolltui_frame_free(f);
    }
    // …and follows the cursor to the next entry
    key(ROLLTUI_KEY_DOWN);
    {
      RolltuiFrame* f = draw(100, 12);
      const auto L = lines_of(text_of(f));
      check(has(L[1], "drwxr-xr-x 2026-01-02 03:04") && !has(L[1], "2026-06-01"), "moving the cursor to another folder changes it to that folder's own [" + L[1] + "]");
      rolltui_frame_free(f);
    }

    // A FILE, NO PREVIEW: a small pane beside the list — its name, its size, and the same second row
    rolltui_picker_go_to(p, file_p.data(), file_p.size());
    {
      RolltuiFrame* f = draw(100, 12);
      const auto L = lines_of(text_of(f));
      check(has(L[0], "plain.txt") && has(L[0], "12 B"), "on a file with no preview, the pane's head has its name and its size [" + L[0] + "]");
      check(has(L[1], "-rw------- 2025-12-31 23:59"), "…and the row under it its mode and time [" + L[1] + "]");
      rolltui_frame_free(f);
    }
    // a size column already says the size: the pane does not say it twice
    on(ROLLTUI_PREVIEW_OFF, ROLLTUI_SHOW_ALWAYS);
    {
      RolltuiFrame* f = draw(100, 12);
      const auto L = lines_of(text_of(f));
      check(!has(L[0], "12 B") && has(L[0], "plain.txt") && has(L[1], "-rw------- 2025-12-31 23:59"), "…unless a size column says it, and then only the size is left out [" + L[0] + "]");
      rolltui_frame_free(f);
    }
    // WITH A PREVIEW: the caption sits between the preview's head and its body
    on(ROLLTUI_PREVIEW_RIGHT, ROLLTUI_SHOW_WITH_SORT);
    rolltui_picker_go_to(p, file_p.data(), file_p.size());
    {
      RolltuiFrame* f = draw(100, 12);
      const auto L = lines_of(text_of(f));
      check(has(L[0], "text \xC2\xB7 1 line \xC2\xB7 12 B") && has(L[1], "-rw------- 2025-12-31 23:59") && has(L[2], "twelve bytes"),
            "with the preview on, the head, then the mode and time, then the file [" + L[0] + " / " + L[1] + " / " + L[2] + "]");
      rolltui_frame_free(f);
    }

    // THE COLUMNS LOSE THE ROW THE BAND TOOK, and a picker too short to spare it keeps its list
    on(ROLLTUI_PREVIEW_OFF, ROLLTUI_SHOW_WITH_SORT);
    rolltui_picker_go_to(p, many_p.data(), many_p.size());
    {
      RolltuiFrame* f = draw(100, 6);
      const std::string t = text_of(f);
      check(has(t, "f3.txt") && !has(t, "f4.txt"), "six rows high: the head, the info, then four rows of list");
      rolltui_frame_free(f);
      f = draw(100, 4);
      const std::string t4 = text_of(f);
      check(has(t4, "f2.txt") && !has(t4, "-rw-r--r--") && !has(t4, "2026-01-02"), "four rows high: no room to spare, so no info, and all three list rows kept");
      rolltui_frame_free(f);
    }
    plain_options();
    {
      RolltuiFrame* f = draw(100, 6);
      const std::string t = text_of(f);
      check(has(t, "f4.txt"), "the same six rows with `info` off hold five rows of list");
      rolltui_frame_free(f);
    }

    // THE POINTER IS HIT-TESTED AGAINST WHAT IS ON SCREEN: with the band two rows deep, a press on a list row selects the entry drawn there.
    // (The cursor is put back on the first entry before each press: the picker remembers where it was in every folder, and a press that
    // landed on the wrong row must not look right because the cursor was already there.)
    auto press_row = [&](int y) {
      rolltui_picker_go_to(p, many_p.data(), many_p.size());
      key(ROLLTUI_KEY_HOME);
      RolltuiFrame* f = draw(100, 12);
      const auto L = lines_of(text_of(f));
      rolltui_frame_free(f);
      int cell = -1;  // the frame's text has 3-byte box glyphs: a cell is a UTF-8 lead byte, not a byte
      for (const std::string& l : L) {  // f1 is in the list only: the pane names the cursor's f0
        const std::size_t at = l.find("f1.txt");
        if (at == std::string::npos) continue;
        cell = 0;
        for (std::size_t i = 0; i < at; ++i) if ((static_cast<unsigned char>(l[i]) & 0xC0) != 0x80) ++cell;
        break;
      }
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_MOUSE;
      e.mouse.kind = RolltuiMouseEvent::Kind::Press;
      e.mouse.button = 1;
      e.mouse.x = cell + 1;
      e.mouse.y = y;
      rolltui_picker_handle(p, &e, b, A);
      RolltuiStr sel{};
      int is_dir = 0;
      rolltui_picker_selected(p, &sel, &is_dir);
      const std::string got_path = str(sel);
      rolltui_str_free(&sel);
      return got_path.substr(got_path.rfind('/') + 1);
    };
    check(press_row(1 + 3) == "f3.txt", "with `info` off, a press on the fourth list row (row 4) selects f3.txt");
    on(ROLLTUI_PREVIEW_OFF, ROLLTUI_SHOW_WITH_SORT);
    { const std::string got5 = press_row(2 + 5); check(got5 == "f5.txt", "with `info` on the list is one row lower: a press on the sixth list row (row 7) selects f5.txt [" + got5 + "]"); }
    { const std::string on_head = press_row(0), on_info = press_row(1); check(on_info == on_head && on_head == "f0.txt", "…a press on the info row is the head's, the same as a press on the head, and moves nothing [" + on_head + " / " + on_info + "]"); }
    plain_options();

    if (had_tz) ::setenv("TZ", kept_tz.c_str(), 1);
    else ::unsetenv("TZ");
    ::tzset();
  }
  // ---- the seventh batch: changes told, the folders visited, a scroll margin, wrapping, half a page ----------------------------
  {
    RolltuiPicker* q = rolltui_picker_new();
    RolltuiPickerOptions o;
    rolltui_picker_options_init(&o);
    o.report_changes = 1;
    o.wrap = 1;
    o.scrolloff = 2;
    rolltui_picker_set_options(q, &o);
    auto press = [&](unsigned char k, bool alt = false, char32_t ch = 0) {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_KEY;
      e.key.key = ch ? static_cast<unsigned char>(ROLLTUI_KEY_CHAR) : k;
      e.key.ch = ch;
      e.key.alt = alt ? 1 : 0;
      return rolltui_picker_handle(q, &e, b, A);
    };
    auto drain = [&](unsigned char kind) {
      RolltuiPickerEvent e{};
      std::string last;
      while (rolltui_picker_event(q, &e))
        if (e.kind == kind) last = str(e.path);
      rolltui_picker_event_release(&e);
      return last;
    };
    auto selected_name = [&] {
      RolltuiStr sel{};
      int is_dir = 0;
      rolltui_picker_selected(q, &sel, &is_dir);
      const std::string got_path = str(sel);
      rolltui_str_free(&sel);
      return got_path.substr(got_path.rfind('/') + 1);
    };
    rolltui_picker_layout(q, RolltuiRect{0, 0, 80, 8});
    const std::string beta = (root / "beta").string();
    rolltui_picker_go_to(q, beta.data(), beta.size());
    press(ROLLTUI_KEY_DOWN);
    check(!drain(ROLLTUI_PICKER_EVENT_MOVED).empty(), "with report_changes, a move is told as a MOVED event with the entry's path");
    const std::string a = (root / "alpha").string();
    rolltui_picker_go_to(q, a.data(), a.size());
    press(ROLLTUI_KEY_DOWN);
    check(drain(ROLLTUI_PICKER_EVENT_FOLDER) == a, "…and a folder opened, as FOLDER with the folder's [" + a + "]");
    {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_KEY;
      e.key.key = ROLLTUI_KEY_LEFT;
      e.key.alt = 1;
      rolltui_picker_handle(q, &e, b, A); /* Alt-Left: back */
    }
    RolltuiStr dir{};
    rolltui_picker_dir(q, &dir);
    check(str(dir) == beta, "Alt-Left goes back to the folder visited before [" + str(dir) + "]");
    {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_KEY;
      e.key.key = ROLLTUI_KEY_RIGHT;
      e.key.alt = 1;
      rolltui_picker_handle(q, &e, b, A); /* Alt-Right: forward */
    }
    rolltui_picker_dir(q, &dir);
    check(str(dir) == a, "…and Alt-Right forward again");
    press(0, false, '-');
    rolltui_picker_dir(q, &dir);
    check(str(dir) == beta, "- goes to the folder last left");
    // WRAPPING and a margin: up from the first entry is the last
    for (int i = 0; i < 20; ++i) write_file(root / "many" / ((i < 10 ? "m0" : "m") + std::to_string(i) + ".txt"), "m");
    const std::string many = (root / "many").string();
    rolltui_picker_go_to(q, many.data(), many.size());
    press(ROLLTUI_KEY_RIGHT);
    press(ROLLTUI_KEY_END);
    const std::string last = selected_name();
    press(ROLLTUI_KEY_HOME);
    const std::string first = selected_name();
    press(ROLLTUI_KEY_UP);
    check(selected_name() == last, "with wrap, Up from the first entry is the last [" + selected_name() + "]");
    press(ROLLTUI_KEY_DOWN);
    check(selected_name() == first, "…and Down from the last the first");
    press(ROLLTUI_KEY_HOME);
    auto index_of = [&] { return std::atoi(selected_name().c_str() + 1); }; /* m07.txt is the eighth */
    {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_KEY;
      e.key.key = ROLLTUI_KEY_CHAR;
      e.key.ch = 'd';
      e.key.ctrl = 1;
      rolltui_picker_handle(q, &e, b, A); /* Ctrl-D: half a page */
    }
    const int half = index_of();
    press(ROLLTUI_KEY_HOME);
    press(ROLLTUI_KEY_PAGEDOWN);
    const int page = index_of();
    check(half > 0 && half * 2 <= page + 1 && half * 2 >= page - 1, "Ctrl-D moves half a page [" + std::to_string(half) + " of " + std::to_string(page) + "]");
    rolltui_str_free(&dir);
    rolltui_picker_free(q);
  }
  // ---- a host's previewer, a preview's lines, ratios, the entry last used, marks and a host's own marks ----------------------------
  {
    write_file(root / "prev" / "long.txt", std::string("a\tb ") + std::string(70, 'x') + "\nsecond line\n");
    write_file(root / "prev" / "page.pdf", "%PDF");
    fs::create_directories(root / "prev" / "sub");
    write_file(root / "prev" / "sub" / "inner.txt", "i");
    RolltuiPicker* q = rolltui_picker_new();
    RolltuiPickerOptions o;
    rolltui_picker_options_init(&o);
    o.preview = ROLLTUI_PREVIEW_RIGHT;
    rolltui_picker_set_options(q, &o);
    struct Host {
      int asked = 0;
      std::size_t last_offset = 99;
    } host;
    rolltui_picker_set_previewer(q, [](void* c, const char* path, std::size_t len, std::size_t offset, RolltuiAnsiText* out, std::size_t* bound) {
      Host* h = static_cast<Host*>(c);
      const std::string pth(path, len);
      if (pth.size() < 4 || pth.substr(pth.size() - 4) != ".pdf") return 0;
      ++h->asked;
      h->last_offset = offset;
      *bound = 3; /* three pages */
      const std::string page = "PAGE " + std::to_string(offset + 1) + " OF 3\n";
      rolltui_ansi_feed(out, page.data(), page.size());
      return 1;
    }, &host);
    auto draw_q = [&](int w, int h) {
      RolltuiFrame* f = rolltui_frame_new(w, h, styles[ROLLTUI_ROLE_BACKGROUND]);
      rolltui_picker_layout(q, RolltuiRect{0, 0, w, h});
      rolltui_picker_draw(q, f, styles, &glyphs, 0, 1);
      const std::string t = text_of(f);
      rolltui_frame_free(f);
      return t;
    };
    auto keyq = [&](unsigned char k) {
      RolltuiEvent e{};
      e.kind = ROLLTUI_EVENT_KEY;
      e.key.key = k;
      return rolltui_picker_handle(q, &e, b, A);
    };
    const std::string pdf = (root / "prev" / "page.pdf").string();
    rolltui_picker_go_to(q, pdf.data(), pdf.size());
    std::string t = draw_q(100, 10);
    check(has(t, "PAGE 1 OF 3") && host.asked > 0, "a host's previewer shows the file it takes, in place of the built-in preview");
    keyq(ROLLTUI_KEY_RIGHT); /* the keys into the preview */
    keyq(ROLLTUI_KEY_DOWN);
    t = draw_q(100, 10);
    check(host.last_offset == 1 && has(t, "PAGE 2 OF 3"), "…and a scroll there asks it again at the next place it has");
    keyq(ROLLTUI_KEY_END);
    keyq(ROLLTUI_KEY_DOWN);
    check(host.last_offset == 2, "…to its last, and no further [" + std::to_string(host.last_offset) + "]");
    {
      RolltuiScrollExtent ext{};
      draw_q(100, 10);
      check(rolltui_picker_scroll_extent(q, &ext) && ext.total == 3 && ext.first == 2 && ext.visible == 1,
            "…and the window's bar is of the previewer's places, not of a file read before [" + std::to_string(ext.first) + "/" +
                std::to_string(ext.total) + "]");
      check(rolltui_picker_scroll_to(q, 0) && host.last_offset == 0, "…and the bar dragged to the top asks it for its first place");
    }
    keyq(ROLLTUI_KEY_LEFT); /* out of the preview */
    // PREVIEWS KEPT, AND THE NEXT READ AHEAD: on workers, nothing drained between the move and the draw
    {
      write_file(root / "cache" / "a.txt", "ALPHA BODY\n");
      write_file(root / "cache" / "b.txt", "BRAVO BODY\n");
      write_file(root / "cache" / "c.txt", "CHARLIE BODY\n");
      write_file(root / "cache" / "d.txt", "DELTA BODY\n");
      RolltuiTasks* tasks = rolltui_tasks_new(1, nullptr, nullptr);
      RolltuiPicker* r = rolltui_picker_new();
      RolltuiPickerOptions ro;
      rolltui_picker_options_init(&ro);
      ro.preview = ROLLTUI_PREVIEW_RIGHT;
      rolltui_picker_set_options(r, &ro);
      rolltui_picker_set_tasks(r, tasks);
      auto draw_r = [&]() {
        RolltuiFrame* f = rolltui_frame_new(100, 8, styles[ROLLTUI_ROLE_BACKGROUND]);
        rolltui_picker_layout(r, RolltuiRect{0, 0, 100, 8});
        rolltui_picker_draw(r, f, styles, &glyphs, 0, 1);
        const std::string t2 = text_of(f);
        rolltui_frame_free(f);
        return t2;
      };
      auto key_r = [&](unsigned char k) {
        RolltuiEvent e{};
        e.kind = ROLLTUI_EVENT_KEY;
        e.key.key = k;
        rolltui_picker_handle(r, &e, b, A);
      };
      const std::string a_txt = (root / "cache" / "a.txt").string();
      rolltui_picker_go_to(r, a_txt.data(), a_txt.size());
      draw_r();
      rolltui_tasks_wait_idle(tasks, 5000);
      check(has(draw_r(), "ALPHA BODY"), "a file previewed on the workers shows once they are done");
      key_r(ROLLTUI_KEY_DOWN);
      check(has(draw_r(), "BRAVO BODY"), "the file below was read ahead: it shows the moment the cursor is on it");
      key_r(ROLLTUI_KEY_DOWN);
      key_r(ROLLTUI_KEY_DOWN); /* d.txt: asked for as the cursor passed c.txt, and nothing drained since */
      check(!has(draw_r(), "DELTA BODY"), "…while a file whose read has not come back waits for the workers (the control)");
      key_r(ROLLTUI_KEY_UP);
      key_r(ROLLTUI_KEY_UP);
      check(has(draw_r(), "BRAVO BODY"), "a file looked at before is kept: back on it, it shows at once");
      rolltui_tasks_wait_idle(tasks, 5000);
      rolltui_picker_free(r);
      rolltui_tasks_free(tasks);
    }
    // A FOLDER READ ON THE WORKERS as the cursor moves: there at once when quick, "loading…" when it waits, filled when it is back
    {
      write_file(root / "slow" / "one" / "x1.txt", "x");
      write_file(root / "slow" / "two" / "y1.txt", "y");
      write_file(root / "slow" / "two" / "y2.txt", "y");
      RolltuiTasks* tasks = rolltui_tasks_new(1, nullptr, nullptr);
      RolltuiPicker* r = rolltui_picker_new();
      RolltuiPickerOptions ro;
      rolltui_picker_options_init(&ro);
      ro.no_watch = 1;
      ro.motion = 0; /* the clock is held still: the columns snap rather than slide */
      rolltui_picker_set_options(r, &ro);
      rolltui_picker_set_tasks(r, tasks);
      auto draw_r = [&]() {
        RolltuiFrame* f = rolltui_frame_new(90, 8, styles[ROLLTUI_ROLE_BACKGROUND]);
        rolltui_picker_layout(r, RolltuiRect{0, 0, 90, 8});
        rolltui_picker_draw(r, f, styles, &glyphs, 0, 1);
        const std::string t2 = text_of(f);
        rolltui_frame_free(f);
        return t2;
      };
      auto key_r = [&](unsigned char k) {
        RolltuiEvent e{};
        e.kind = ROLLTUI_EVENT_KEY;
        e.key.key = k;
        rolltui_picker_handle(r, &e, b, A);
      };
      auto block_worker = [&]() { /* the one worker held, so a read waits behind it */
        rolltui_tasks_start(tasks, [](void*, const RolltuiJob*) { std::this_thread::sleep_for(std::chrono::milliseconds(300)); },
                            [](void*, int) {}, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      };
      const std::string slow = (root / "slow").string();
      rolltui_picker_set_now(r, 1000);
      rolltui_picker_go_to(r, slow.data(), slow.size());
      draw_r();
      key_r(ROLLTUI_KEY_DOWN); /* onto two: read while the move waits */
      check(has(draw_r(), "y1.txt"), "a folder read quickly on the workers is beside the cursor in the same frame");
      key_r(ROLLTUI_KEY_UP);
      block_worker();
      key_r(ROLLTUI_KEY_DOWN);
      std::string t2 = draw_r();
      check(!has(t2, "y1.txt") && !has(t2, "loading"), "a read that has not come back draws nothing at first");
      RolltuiPickerStatus st{};
      rolltui_picker_status(r, &st);
      check(st.wake_ms > 0 && st.wake_ms <= 100, "…and asks for a frame when \"loading…\" is due [" + std::to_string(st.wake_ms) + "]");
      rolltui_picker_status_release(&st);
      rolltui_picker_set_now(r, 1150);
      check(has(draw_r(), "loading\xE2\x80\xA6"), "…then says \"loading…\" once it has taken 100 ms");
      rolltui_tasks_wait_idle(tasks, 5000);
      t2 = draw_r();
      check(has(t2, "y1.txt") && has(t2, "y2.txt") && !has(t2, "loading"), "…and shows the folder when its read is back");
      // INTO A FOLDER STILL BEING READ: the cursor lands in it when the read is back
      key_r(ROLLTUI_KEY_UP);
      rolltui_tasks_wait_idle(tasks, 5000);
      key_r(ROLLTUI_KEY_DOWN);
      rolltui_tasks_wait_idle(tasks, 5000);
      block_worker();
      key_r(ROLLTUI_KEY_UP); /* onto one, its read held */
      key_r(ROLLTUI_KEY_RIGHT);
      rolltui_tasks_wait_idle(tasks, 5000);
      RolltuiStr sel{};
      int sel_dir = 0;
      rolltui_picker_selected(r, &sel, &sel_dir);
      const std::string sel_s(sel.p ? sel.p : "", sel.n);
      check(sel_s.size() >= 6 && sel_s.substr(sel_s.size() - 6) == "x1.txt",
            "Right into a folder still being read puts the cursor in it once the read is back [" + sel_s + "]");
      rolltui_str_free(&sel);
      rolltui_picker_free(r); /* a read still out when the picker goes is dropped, not shown */
      block_worker();
      rolltui_tasks_free(tasks);
    }
#if !defined(_WIN32)
    // A COMMAND AS THE PREVIEWER: started where the cursor rests, its output the preview, stopped when the cursor moves on
    {
      write_file(root / "cmd" / "one.txt", "1");
      write_file(root / "cmd" / "two.txt", "2");
      write_file(root / "cmd" / "zzz.txt", "3");
      const fs::path pids = root / "cmd_pids";
      fs::create_directories(pids);
      const std::string script = "echo $$ > '" + pids.string() + "'/\"${0##*/}\".pid; printf 'CMD %s %s %s\\n' \"${0##*/}\" \"$1\" \"$2\"; exec sleep 30";
      const char* argv[] = {"/bin/sh", "-c", script.c_str(), nullptr};
      RolltuiPicker* r = rolltui_picker_new();
      RolltuiPickerOptions ro;
      rolltui_picker_options_init(&ro);
      ro.preview = ROLLTUI_PREVIEW_RIGHT;
      ro.motion = 0;
      ro.no_watch = 1;
      rolltui_picker_set_options(r, &ro);
      rolltui_picker_set_preview_command(r, argv);
      auto draw_r = [&]() {
        RolltuiFrame* f = rolltui_frame_new(100, 10, styles[ROLLTUI_ROLE_BACKGROUND]);
        rolltui_picker_layout(r, RolltuiRect{0, 0, 100, 10});
        rolltui_picker_draw(r, f, styles, &glyphs, 0, 1);
        const std::string t2 = text_of(f);
        rolltui_frame_free(f);
        return t2;
      };
      auto key_r = [&](unsigned char k) {
        RolltuiEvent e{};
        e.kind = ROLLTUI_EVENT_KEY;
        e.key.key = k;
        rolltui_picker_handle(r, &e, b, A);
      };
      const std::string one = (root / "cmd" / "one.txt").string();
      rolltui_picker_set_now(r, 10000);
      rolltui_picker_go_to(r, one.data(), one.size());
      draw_r();
      key_r(ROLLTUI_KEY_DOWN); /* onto two.txt at once: one.txt was passed */
      std::string t2 = draw_r();
      check(!has(t2, "CMD"), "the command waits for the cursor to rest");
      rolltui_picker_set_now(r, 10040);
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(3000 * rolltui_test::kClockScale));
      while (!has(t2 = draw_r(), "CMD two.txt") && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      check(has(t2, "CMD two.txt"), "…then its output is the preview of the file it rests on");
      {
        std::size_t at = t2.find("CMD two.txt ");
        int cw2 = 0, ch2 = 0;
        if (at != std::string::npos) std::sscanf(t2.c_str() + at + 12, "%d %d", &cw2, &ch2);
        check(cw2 > 10 && ch2 > 2, "…given the preview's width and height after the file's path [" + std::to_string(cw2) + "x" + std::to_string(ch2) + "]");
      }
      check(!fs::exists(pids / "one.txt.pid"), "a file the cursor passed over started nothing");
      pid_t two_pid = 0;
      {
        std::ifstream in(pids / "two.txt.pid");
        in >> two_pid;
      }
      key_r(ROLLTUI_KEY_DOWN); /* on to zzz.txt */
      check(two_pid > 0 && kill(two_pid, 0) != 0, "moving on stops the command that was running [" + std::to_string(two_pid) + "]");
      rolltui_picker_free(r);
    }
#endif
    // A LINE WRAPPED, at the tab stop set
    const std::string longp = (root / "prev" / "long.txt").string();
    o.preview_wrap = 1;
    o.preview_tab = 8;
    rolltui_picker_set_options(q, &o);
    rolltui_picker_go_to(q, longp.data(), longp.size());
    t = draw_q(100, 12);
    std::size_t xs = 0, x_rows = 0;
    const std::vector<std::string> wrap_lines = lines_of(t);
    for (std::size_t li = 1; li < wrap_lines.size(); ++li) { /* the preview's body (below its caption) is right of the last divider */
      const std::string& l = wrap_lines[li];
      const std::size_t bar = l.rfind("\xE2\x94\x82");
      const std::string right = bar == std::string::npos ? "" : l.substr(bar + 3);
      const std::size_t n = static_cast<std::size_t>(std::count(right.begin(), right.end(), 'x'));
      xs += n;
      x_rows += n ? 1 : 0;
    }
    check(has(t, "a       b x"), "a tab reaches the stop set for the preview: eight columns");
    check(xs == 70 && x_rows == 2 && !has(t, "x\xE2\x80\xA6"),
          "…and a line too long wraps onto the next row, all of it there [" + std::to_string(xs) + " in " + std::to_string(x_rows) + " rows]");
    // SIDEWAYS: a long line's preview, not wrapped, scrolled right and back, as far as its end
    {
      o.preview_wrap = 0;
      rolltui_picker_set_options(q, &o);
      rolltui_picker_go_to(q, longp.data(), longp.size());
      draw_q(100, 12);
      RolltuiEvent sr{};
      sr.kind = ROLLTUI_EVENT_KEY;
      sr.key.key = ROLLTUI_KEY_RIGHT;
      sr.key.shift = 1;
      rolltui_picker_handle(q, &sr, b, A);
      t = draw_q(100, 12);
      check(has(t, "b xxx") && !has(t, "a       b"), "Shift-Right scrolls the preview's lines sideways, from the list");
      for (int i = 0; i < 20; ++i) rolltui_picker_handle(q, &sr, b, A);
      t = draw_q(100, 12);
      check(has(t, "xxxx") && !has(t, "x\xE2\x80\xA6"), "…as far as the widest line's end shown whole, and no further");
      RolltuiEvent sl = sr;
      sl.key.key = ROLLTUI_KEY_LEFT;
      for (int i = 0; i < 30; ++i) rolltui_picker_handle(q, &sl, b, A);
      check(has(draw_q(100, 12), "a       b x"), "Shift-Left scrolls back to the lines' start");
      // CODE: its colours move with its text
      write_file(root / "prev" / "wide.c", "int a = 1; /* " + std::string(80, 'y') + " */\n");
      const std::string widec = (root / "prev" / "wide.c").string();
      rolltui_picker_go_to(q, widec.data(), widec.size());
      draw_q(100, 12);
      rolltui_picker_handle(q, &sr, b, A);
      RolltuiFrame* f = rolltui_frame_new(100, 12, styles[ROLLTUI_ROLE_BACKGROUND]);
      rolltui_picker_layout(q, RolltuiRect{0, 0, 100, 12});
      rolltui_picker_draw(q, f, styles, &glyphs, 0, 1);
      t = text_of(f);
      int yx = -1, yy = -1;
      for (int y = 0; y < 12 && yx < 0; ++y)
        for (int x = 60; x < 100; ++x) {
          RolltuiCellRaw c{};
          rolltui_frame_cell(f, x, y, &c);
          if (c.len == 1 && c.bytes[0] == 'y') { yx = x, yy = y; break; }
        }
      RolltuiCellRaw yc{};
      if (yx >= 0) rolltui_frame_cell(f, yx, yy, &yc);
      check(!has(t, "int a") && has(t, "/* yyy") && yx >= 0 && yc.style.italic,
            "a line of code scrolls sideways with its classes in step: the comment still drawn as a comment");
      rolltui_frame_free(f);
    }
    // THE ENTRY LAST USED, marked in the folder beside the cursor
    const std::string sub = (root / "prev" / "sub").string();
    rolltui_picker_go_to(q, (root / "prev" / "sub" / "inner.txt").string().data(), (root / "prev" / "sub" / "inner.txt").string().size());
    keyq(ROLLTUI_KEY_LEFT); /* the cursor on sub, in prev: sub's listing beside it, inner.txt its last used */
    {
      RolltuiFrame* f = rolltui_frame_new(100, 10, styles[ROLLTUI_ROLE_BACKGROUND]);
      rolltui_picker_layout(q, RolltuiRect{0, 0, 100, 10});
      rolltui_picker_draw(q, f, styles, &glyphs, 0, 1);
      int ix = -1, iy = -1;
      for (int y = 0; y < 10 && ix < 0; ++y)
        for (int x = 0; x < 100; ++x) {
          RolltuiCellRaw c{};
          rolltui_frame_cell(f, x, y, &c);
          if (c.len == 1 && c.bytes[0] == 'i' && x + 5 < 100) { /* "inner", the entry itself: not "info" in a column further up */
            bool word = true;
            for (int k = 1; k < 5 && word; ++k) {
              RolltuiCellRaw n1{};
              rolltui_frame_cell(f, x + k, y, &n1);
              word = n1.len == 1 && n1.bytes[0] == "inner"[k];
            }
            if (word) { ix = x, iy = y; break; }
          }
        }
      RolltuiCellRaw c{};
      if (ix >= 0) rolltui_frame_cell(f, ix, iy, &c);
      check(ix >= 0 && c.style.bg.r == styles[ROLLTUI_ROLE_SELECTION].bg.r && c.style.bg.g == styles[ROLLTUI_ROLE_SELECTION].bg.g,
            "the folder beside the cursor marks the entry last used in it, in the selection's style");
      rolltui_frame_free(f);
    }
    // RATIOS: a fifth, a half and the rest
    o.preview_wrap = 0;
    o.ratio_parent = 1, o.ratio_current = 2, o.ratio_next = 1;
    rolltui_picker_set_options(q, &o);
    rolltui_picker_go_to(q, longp.data(), longp.size());
    {
      RolltuiFrame* f = rolltui_frame_new(102, 10, styles[ROLLTUI_ROLE_BACKGROUND]);
      rolltui_picker_layout(q, RolltuiRect{0, 0, 102, 10});
      rolltui_picker_draw(q, f, styles, &glyphs, 0, 1);
      std::vector<int> bars;
      for (int x = 0; x < 102; ++x) {
        RolltuiCellRaw c{};
        rolltui_frame_cell(f, x, 9, &c); /* the bottom row: past every name, so nothing but the dividers */
        if (c.len == 3 && std::memcmp(c.bytes, "\xE2\x94\x82", 3) == 0) bars.push_back(x);
      }
      /* 100 cells less the two dividers: a quarter, a half and a quarter */
      check(bars.size() == 2 && bars[0] == 25 && bars[1] == 76,
            "ratios of 1, 2 and 1 give the panes a quarter, a half and a quarter [" + (bars.empty() ? std::string("none") : std::to_string(bars[0]) + "," + std::to_string(bars.back())) + "]");
      rolltui_frame_free(f);
    }
    // MARKS, and a host's own glyph and cell
    o.ratio_parent = o.ratio_current = o.ratio_next = 0;
    o.preview = ROLLTUI_PREVIEW_OFF;
    o.marks = 1;
    rolltui_picker_set_options(q, &o);
    const std::string beta = (root / "beta").string();
    rolltui_picker_go_to(q, beta.data(), beta.size());
    keyq(ROLLTUI_KEY_RIGHT);
    {
      RolltuiEvent sp{};
      sp.kind = ROLLTUI_EVENT_KEY;
      sp.key.key = ROLLTUI_KEY_CHAR;
      sp.key.ch = ' ';
      rolltui_picker_handle(q, &sp, b, A);
      rolltui_picker_handle(q, &sp, b, A);
    }
    RolltuiStrList marked;
    check(rolltui_picker_marked(q, &marked) == 2 && marked.n == 2, "Space marks an entry and moves on: two marked");
    rolltui_picker_set_decor(q, [](void*, const char* path, std::size_t len, int, RolltuiPickerDecor* out) {
      const std::string pth(path, len);
      if (pth.find("b3.txt") != std::string::npos) {
        std::memcpy(out->gutter, "C", 2);
        std::memcpy(out->cell, "M", 2);
      }
    }, nullptr);
    t = draw_q(80, 18);
    bool gutter = false, cell = false, mark = false;
    for (const std::string& l : lines_of(t)) {
      gutter = gutter || l.find("Cb3") != std::string::npos;
      cell = cell || (l.find("b3") != std::string::npos && l.find(" M") != std::string::npos);
      mark = mark || l.find("\xE2\x96\x8C" "b0") != std::string::npos;
    }
    check(mark, "a marked entry has ▌ before it");
    check(gutter && cell, "a host's own glyph stands before an entry, and its cell beside the name");
    {
      RolltuiEvent u{};
      u.kind = ROLLTUI_EVENT_KEY;
      u.key.key = ROLLTUI_KEY_CHAR;
      u.key.ch = 'u';
      u.key.alt = 1;
      rolltui_picker_handle(q, &u, b, A);
    }
    check(rolltui_picker_marked(q, nullptr) == 0, "Alt-U clears every mark");
    rolltui_picker_free(q);
  }
  rolltui_picker_event_release(&ev);
  rolltui_str_free(&got);
  rolltui_picker_free(p);
  rolltui_context_free(ctx);
  fs::remove_all(root, ec);
  return testkit::report("rolltui_picker_test");
}
