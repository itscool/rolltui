// rolltui/tests/picker_test.cpp — the column browser behind `filepicker`, driven directly.
//
// `picker_kind_test` is the consumer-shaped suite: a layout names the kind and a host makes the
// window calls. This one opts into the internal header to assert what a host cannot see and
// a text frame cannot show: the fade at the left edge, the per-folder memory, the anchor's
// slot, the dividers and their thumbs, the marks. It builds a tree of its own and reads the
// frame it draws.
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_picker.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_screen.h"  /* INTERNAL: a frame of its own to draw into */
#include "rolltui_test.hpp"
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
    rolltui_picker_draw(p, f, styles, &glyphs, 0);
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
  // ---- the status line's facts ---------------------------------------------------------------
  {
    RolltuiPickerStatus st{};
    rolltui_picker_status(p, &st);
    check(st.entries == 12 && st.columns >= 2 && st.column == st.columns && st.error.n == 0,
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
  rolltui_picker_event_release(&ev);
  rolltui_str_free(&got);
  rolltui_picker_free(p);
  rolltui_context_free(ctx);
  fs::remove_all(root, ec);
  return testkit::report("rolltui_picker_test");
}
