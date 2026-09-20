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
#include <vector>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_widget_picker.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
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
      (void)ex;
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
        rolltui_picker_draw(q, fr, styles, &glyphs, 0);
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
  rolltui_picker_event_release(&ev);
  rolltui_str_free(&got);
  rolltui_picker_free(p);
  rolltui_context_free(ctx);
  fs::remove_all(root, ec);
  return testkit::report("rolltui_picker_test");
}
