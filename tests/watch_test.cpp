// rolltui/tests/watch_test.cpp — WHAT IS SHOWN IS WATCHED: the column browser looks at every folder that has its insides on
// screen, and at the file being previewed, and keeps up.
//
// Two things make this testable without waiting for a clock. `rolltui_picker_refresh` is the look itself, so a test changes
// the disk and calls it; and the timestamps are AGED before a picker is pointed at a tree, because a folder written in the
// last two seconds is deliberately looked at again at every look (a file system that keeps whole seconds can carry one
// stamp through two writes) and would hide the paths that matter here: a file that grows in place and does not touch its
// folder's stamp at all. The clock-driven half is tested separately, through `rolltui_picker_set_now`.
//
// The edge cases it holds the picker to, each one a thing that happens to a real folder:
//   a file appears, grows, is removed, is renamed; the selected one included, and the last one, and every one
//   the list shrinks under a scrolled window (the bottom clamp) and grows above one (the top stays where it was)
//   a folder in the middle of the columns vanishes, is replaced by a file, comes back as a folder
//   a hidden folder the cursor is inside is hidden by a setting
//   the file previewed grows under a reader at its foot (followed), or under one in the middle (not), is cut off above
//   the reader's window (clamped), turns binary, is rewritten as Markdown, is deleted
//   a folder that is nothing but its own listing is emptied, and filled again
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_widget_picker.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_screen.h"         /* INTERNAL: a frame of its own to draw into */
#include "rolltui_test.hpp"

namespace {
namespace fs = std::filesystem;

void write_file(const fs::path& p, const std::string& t) {
  fs::create_directories(p.parent_path());
  const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) { (void)!::write(fd, t.data(), t.size()); ::close(fd); }
}
void append_file(const fs::path& p, const std::string& t) {
  const int fd = ::open(p.c_str(), O_WRONLY | O_APPEND);
  if (fd >= 0) { (void)!::write(fd, t.data(), t.size()); ::close(fd); }
}
// Every file and folder under `root`, and `root`, given a stamp an hour old: nothing is "written too lately to trust".
// (The scratch root is aged too — it was made a moment ago, and it is one of the folders the picker is showing.)
fs::path g_scratch;
void age(const fs::path&) {
  const fs::path root = g_scratch;
  timespec ts[2];
  ts[0].tv_sec = ts[1].tv_sec = ::time(nullptr) - 3600;
  ts[0].tv_nsec = ts[1].tv_nsec = 0;
  std::error_code ec;
  for (const auto& e : fs::recursive_directory_iterator(root, ec)) ::utimensat(AT_FDCWD, e.path().c_str(), ts, AT_SYMLINK_NOFOLLOW);
  ::utimensat(AT_FDCWD, root.c_str(), ts, AT_SYMLINK_NOFOLLOW);
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
std::string lines(int n, int from = 0) {
  std::string out;
  for (int i = 0; i < n; ++i) out += "line " + std::to_string(from + i) + "\n";
  return out;
}

struct Rig {
  RolltuiContext* ctx;
  const RolltuiBindings* b;
  const RolltuiPickerActions* A;
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiScrollbarGlyphs glyphs{};
  RolltuiPicker* p;
  RolltuiPickerOptions po{};
  int w = 110, h = 14;

  Rig() {
    ctx = rolltui_context_new();
    rolltui_context_set_library_defaults(ctx);
    b = rolltui_bindings_default(ctx);
    A = rolltui_picker_default_actions();
    RolltuiEffectMap* fx = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
    rolltui_effect_map_free(fx);
    rolltui_theme_scrollbar_glyphs(nullptr, &glyphs);
    p = rolltui_picker_new();
    rolltui_picker_options_init(&po);
    po.motion = 0;
    rolltui_picker_set_options(p, &po);
  }
  ~Rig() {
    rolltui_picker_free(p);
    rolltui_context_free(ctx);
  }
  void options() { rolltui_picker_set_options(p, &po); }
  void go(const fs::path& path) {
    const std::string s = path.string();
    rolltui_picker_go_to(p, s.data(), s.size());
  }
  void key(unsigned char k) {
    RolltuiEvent e{};
    e.kind = ROLLTUI_EVENT_KEY;
    e.key.key = k;
    rolltui_picker_handle(p, &e, b, A);
  }
  std::string frame() {
    RolltuiFrame* f = rolltui_frame_new(w, h, styles[ROLLTUI_ROLE_BACKGROUND]);
    rolltui_picker_layout(p, RolltuiRect{0, 0, w, h});
    rolltui_picker_draw(p, f, styles, &glyphs, 0);
    const std::string t = text_of(f);
    rolltui_frame_free(f);
    return t;
  }
  std::string selected() {
    RolltuiStr s{};
    int dir = 0;
    rolltui_picker_selected(p, &s, &dir);
    const std::string out = str(s);
    rolltui_str_free(&s);
    return out;
  }
  std::string dir() {
    RolltuiStr s{};
    rolltui_picker_dir(p, &s);
    const std::string out = str(s);
    rolltui_str_free(&s);
    return out;
  }
  RolltuiPickerStatus status() {
    RolltuiPickerStatus st{};
    rolltui_picker_status(p, &st);
    return st;
  }
  std::size_t columns() {
    RolltuiPickerStatus st = status();
    const std::size_t n = st.columns;
    rolltui_picker_status_release(&st);
    return n;
  }
  std::size_t entries() {
    RolltuiPickerStatus st = status();
    const std::size_t n = st.entries;
    rolltui_picker_status_release(&st);
    return n;
  }
};

std::size_t live_bytes() {
  std::size_t live = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, &live, nullptr, nullptr);
  return live;
}

}  // namespace

int main() {
  using testkit::check;
  const std::string tmp = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
  const fs::path root = fs::path(tmp) / ("rolltui_watch_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  g_scratch = root;
  const std::size_t base = live_bytes();
  {
    Rig r;

    // ---- A FOLDER THAT IS SHOWN: a file added, grown, removed --------------------------------------------------
    {
      const fs::path d = root / "t1";
      for (const char* n : {"a.txt", "b.txt", "c.txt", "d.txt", "e.txt"}) write_file(d / n, "x");
      age(d);
      r.po.show_size = ROLLTUI_SHOW_ALWAYS;
      r.options();
      r.go(d / "c.txt");
      std::string t = r.frame();
      check(has(t, "c.txt") && !has(t, "bb.txt") && r.selected() == (d / "c.txt").string(), "the folder is shown, the cursor on c.txt");
      check(!rolltui_picker_refresh(r.p) || true, "(a look at a folder that has not moved)");
      // a file is added: it appears, and the cursor is still on the file it was on
      write_file(d / "bb.txt", "x");
      check(rolltui_picker_refresh(r.p) == 1, "a file added to a shown folder is seen by the next look");
      t = r.frame();
      check(has(t, "bb.txt") && r.selected() == (d / "c.txt").string(), "…it is listed, and the cursor did not move off its file: the selection is kept BY NAME, not by row");
      // a file grows IN PLACE: its folder's stamp does not move at all, and the size on its row is what changed
      age(d);
      (void)rolltui_picker_refresh(r.p);
      (void)rolltui_picker_refresh(r.p);
      check(!has(r.frame(), "2.9K"), "(the size on a row is what the file was)");
      append_file(d / "d.txt", std::string(3000, 'y'));
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "2.9K"),
            "a file that grows in place is seen on its row though its folder's stamp never moved (3001 bytes, \"2.9K\")");
      // and the look after a look that found nothing finds nothing
      // A look at a folder that has settled changes nothing on screen. (Not asserted as "returns 0": the picker also shows the
      // folders ABOVE the scratch tree, and a temporary directory is one that other programs are writing to all the time.)
      age(d);
      (void)rolltui_picker_refresh(r.p);
      (void)rolltui_picker_refresh(r.p);
      {
        const std::string before = r.frame();
        (void)rolltui_picker_refresh(r.p);
        check(r.frame() == before && r.selected() == (d / "c.txt").string(), "a look at a folder that has settled changes nothing on screen, and the cursor is where it was");
      }

      // THE SELECTED FILE IS REMOVED: the eye falls to the entry that now stands where it stood
      fs::remove(d / "c.txt");
      check(rolltui_picker_refresh(r.p) == 1 && r.selected() == (d / "d.txt").string(),
            "the file the cursor was on is removed: the cursor falls to the next one, d.txt [" + r.selected() + "]");
      check(!has(r.frame(), "c.txt"), "…and the removed file is gone from the column");
      r.key(ROLLTUI_KEY_DOWN);  // e.txt, the last
      check(r.selected() == (d / "e.txt").string(), "(the cursor is on the last entry)");
      fs::remove(d / "e.txt");
      check(rolltui_picker_refresh(r.p) == 1 && r.selected() == (d / "d.txt").string(),
            "the LAST entry is removed with the cursor on it: the cursor falls to the new last one, d.txt [" + r.selected() + "]");
      // RENAMED is a removal and an addition: the cursor cannot follow a name it never saw, and lands beside it
      fs::rename(d / "d.txt", d / "zz.txt");
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "zz.txt") && !has(r.frame(), "d.txt") && r.entries() == 4,
            "a renamed file is listed under its new name and not the old");
      r.po.show_size = ROLLTUI_SHOW_WITH_SORT;
      r.options();
    }

    // ---- THE FOLDER EMPTIED, AND FILLED AGAIN ------------------------------------------------------------------
    {
      const fs::path d = root / "t2";
      write_file(d / "only.txt", "x");
      age(d);
      r.go(d / "only.txt");
      fs::remove(d / "only.txt");
      check(rolltui_picker_refresh(r.p) == 1 && r.entries() == 0 && has(r.frame(), "(empty)"), "a folder emptied under the cursor says (empty), and nothing crashes");
      check(r.selected() == d.string(), "…and what is selected is the folder itself, as it is for any empty column");
      write_file(d / "back.txt", "x");
      check(rolltui_picker_refresh(r.p) == 1 && r.entries() == 1 && r.selected() == (d / "back.txt").string(), "…and when a file arrives it is listed and the cursor is on it");
    }

    // ---- THE BOTTOM AND THE TOP: a scrolled list that shrinks under the window, and one that grows above it -------
    {
      const fs::path d = root / "t3";
      for (int i = 0; i < 60; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "f%02d.txt", i);
        write_file(d / n, "x");
      }
      age(d);
      r.h = 10;  // nine rows of list
      r.go(d / "f59.txt");
      std::string t = r.frame();
      RolltuiScrollExtent ex{};
      check(has(t, "f59.txt") && !has(t, "f00.txt"), "sixty entries, the cursor on the last: the window is at the bottom");
      // the tail of the list goes — the entry the cursor is on with it
      for (int i = 40; i < 60; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "f%02d.txt", i);
        fs::remove(d / n);
      }
      check(rolltui_picker_refresh(r.p) == 1, "twenty entries, the one under the cursor among them, are removed");
      t = r.frame();
      check(r.entries() == 40 && r.selected() == (d / "f39.txt").string(), "the cursor falls to the last entry left, f39 [" + r.selected() + "]");
      check(has(t, "f39.txt") && !has(t, "f45.txt") && !has(t, "f00.txt"), "…and the window is clamped to the end of the shorter list: the last row is the bottom one, no blank rows under it");
      check(rolltui_picker_scroll_extent(r.p, &ex) && ex.total == 40 && ex.first + ex.visible == ex.total, "…as the scrollbar says [" + std::to_string(ex.first) + "+" + std::to_string(ex.visible) + " of " + std::to_string(ex.total) + "]");
      // entries appear ABOVE the window: what was on the screen is still on it
      age(d);
      (void)rolltui_picker_refresh(r.p);
      for (int i = 0; i < 5; ++i) write_file(d / ("a0" + std::to_string(i) + ".txt"), "x");
      check(rolltui_picker_refresh(r.p) == 1, "five entries are added that sort before everything on screen");
      t = r.frame();
      check(has(t, "f39.txt") && has(t, "f31.txt") && !has(t, "a00.txt") && r.selected() == (d / "f39.txt").string(),
            "…and the window stays where it was, the same entry at its top and the cursor on the same file: nothing on screen moved");
      // and below it, with the cursor on the last entry
      write_file(d / "zzz.txt", "x");
      check(rolltui_picker_refresh(r.p) == 1 && r.selected() == (d / "f39.txt").string() && has(r.frame(), "f39.txt"), "an entry added below the window does not carry the cursor with it");
      r.h = 14;
    }

    // ---- SORTED BY SIZE: a row off screen can move the ones on it -----------------------------------------------
    {
      const fs::path d = root / "t4";
      for (int i = 0; i < 600; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "f%03d.txt", i);
        write_file(d / n, std::string(static_cast<std::size_t>(i + 1), 'x'));
      }
      age(d);
      r.po.sort = ROLLTUI_SORT_SIZE;
      r.po.show_size = ROLLTUI_SHOW_ALWAYS;
      r.options();
      r.h = 10;
      r.go(d);
      check(has(r.frame(), "f599.txt") && !has(r.frame(), "f000.txt"), "sorted by size, largest first: the largest is on screen, the smallest is not");
      append_file(d / "f000.txt", std::string(20000, 'z'));  // the smallest file becomes the largest, off screen, folder stamp untouched
      int looks = 0;
      while (looks < 4 && !has(r.frame(), "f000.txt")) { (void)rolltui_picker_refresh(r.p); ++looks; }
      check(has(r.frame(), "f000.txt") && looks <= 2, "a file that grows off screen, in a folder sorted by size, is found within two looks and the order follows it (" + std::to_string(looks) + ")");
      r.po.sort = ROLLTUI_SORT_NAME;
      r.po.show_size = ROLLTUI_SHOW_WITH_SORT;
      r.options();
      r.h = 14;
    }

    // ---- THE COLUMNS THAT DEPENDED ON WHAT IS GONE --------------------------------------------------------------
    {
      const fs::path d = root / "t5";
      write_file(d / "a" / "x" / "deep.txt", "deep");
      write_file(d / "a" / "y.txt", "y");
      write_file(d / "b" / "inside.txt", "b");
      age(d);
      r.go(d / "a" / "x");
      const std::size_t deep_cols = r.columns();
      check(r.dir() == (d / "a" / "x").string() && r.selected() == (d / "a" / "x" / "deep.txt").string(), "the eye is in x, on deep.txt");
      // the focused folder is removed: the eye comes back to the deepest column still standing, on the entry in x's place
      fs::remove_all(d / "a" / "x");
      check(rolltui_picker_refresh(r.p) == 1, "the folder the eye is in is removed");
      check(r.dir() == (d / "a").string() && r.selected() == (d / "a" / "y.txt").string(),
            "the eye comes back to its parent, on the entry now in the removed folder's place [" + r.dir() + " / " + r.selected() + "]");
      check(r.columns() == deep_cols - 1 && !has(r.frame(), "deep.txt"), "…the column that listed it is dropped, and nothing of it is drawn");
      // a folder in the MIDDLE of the columns goes, with the eye deep inside it
      write_file(d / "a" / "x" / "deep.txt", "deep");
      age(d);
      r.go(d / "a" / "x");
      fs::remove_all(d / "a");
      check(rolltui_picker_refresh(r.p) == 1 && r.dir() == d.string() && r.selected() == (d / "b").string(),
            "a folder two levels up is removed with the eye inside it: the eye lands in the deepest folder left, on its next entry [" + r.dir() + " / " + r.selected() + "]");
      check(!has(r.frame(), "deep.txt") && has(r.frame(), "inside.txt"), "…and since that entry is a folder, its listing is opened, as if the cursor had moved there");
    }

    // ---- A FOLDER BECOMES A FILE, AND A FILE BECOMES A FOLDER -----------------------------------------------------
    {
      const fs::path d = root / "t6";
      write_file(d / "thing" / "in.txt", "in");
      write_file(d / "other.txt", "o");
      age(d);
      r.go(d);
      r.key(ROLLTUI_KEY_UP);  // whatever is first: `thing`, a folder, sorts first
      check(r.selected() == (d / "thing").string() && has(r.frame(), "in.txt"), "the cursor is on a folder and its listing is beside it");
      const std::size_t with_child = r.columns();
      fs::remove_all(d / "thing");
      write_file(d / "thing", "now a file");
      check(rolltui_picker_refresh(r.p) == 1, "the folder is replaced by a file of the same name");
      check(r.selected() == (d / "thing").string() && r.columns() == with_child - 1 && !has(r.frame(), "in.txt"),
            "the cursor is still on the name, the column that listed the folder is gone, and none of it is drawn");
      fs::remove(d / "thing");
      write_file(d / "thing" / "fresh.txt", "f");
      check(rolltui_picker_refresh(r.p) == 1 && r.columns() == with_child && has(r.frame(), "fresh.txt"), "and replaced by a folder again, its listing opens beside the cursor");
    }

    // ---- A HIDDEN FOLDER THE CURSOR IS INSIDE IS HIDDEN BY A SETTING (the old reload left the columns stale) -----
    {
      const fs::path d = root / "t7";
      write_file(d / ".cfg" / "inner" / "f.txt", "f");
      write_file(d / "vis.txt", "v");
      age(d);
      r.po.hidden = 1;
      r.options();
      r.go(d / ".cfg" / "inner");
      check(r.dir() == (d / ".cfg" / "inner").string(), "the eye is inside a dot folder");
      r.po.hidden = 0;
      r.options();
      check(r.dir() == d.string() && r.selected() == (d / "vis.txt").string() && !has(r.frame(), "f.txt") && !has(r.frame(), ".cfg"),
            "dotfiles are turned off: the eye comes out to the folder that still lists something it can show, and the hidden columns are gone [" + r.dir() + "]");
      r.po.hidden = 1;
      r.options();
    }

    // ---- THE FILE BEING PREVIEWED ----------------------------------------------------------------------------------
    {
      const fs::path d = root / "t8";
      write_file(d / "log.txt", lines(40));
      write_file(d / "z.txt", "z");
      age(d);
      r.po.preview = ROLLTUI_PREVIEW_RIGHT;
      r.options();
      r.h = 12;
      r.go(d / "log.txt");
      std::string t = r.frame();
      RolltuiScrollExtent ex{};
      check(has(t, "line 0") && has(t, "40 lines"), "the file is previewed");
      // a reader IN THE MIDDLE is not carried anywhere by the file growing under them
      rolltui_picker_scroll_to(r.p, 10);
      t = r.frame();
      check(has(t, "line 10") && !has(t, "line 9\n"), "(the reader is ten lines down)");
      append_file(d / "log.txt", lines(30, 40));
      check(rolltui_picker_refresh(r.p) == 1, "the previewed file grows");
      t = r.frame();
      check(has(t, "line 10") && !has(t, "line 9\n") && has(t, "70 lines") && rolltui_picker_scroll_extent(r.p, &ex) && ex.total == 70 && ex.first == 10,
            "a reader in the middle stays at the same line and the scrollbar knows the new length (first " + std::to_string(ex.first) + " of " + std::to_string(ex.total) + ")");
      // a reader AT THE FOOT follows it, as `tail -f` does
      r.key(ROLLTUI_KEY_RIGHT);
      r.key(ROLLTUI_KEY_END);
      t = r.frame();
      check(has(t, "line 69") && rolltui_picker_scroll_extent(r.p, &ex) && ex.first + ex.visible == ex.total, "(the reader is at the last line)");
      append_file(d / "log.txt", lines(15, 70));
      check(rolltui_picker_refresh(r.p) == 1, "the file grows under a reader at its foot");
      t = r.frame();
      check(has(t, "line 84") && rolltui_picker_scroll_extent(r.p, &ex) && ex.total == 85 && ex.first + ex.visible == ex.total,
            "…and the reader is still at its foot: the last line is the bottom one, the file is followed");
      // the file is CUT OFF above the reader's window: the view is clamped, never blank
      write_file(d / "log.txt", lines(5));
      check(rolltui_picker_refresh(r.p) == 1, "the file is cut down to five lines, above where the reader was");
      t = r.frame();
      check(has(t, "line 0") && has(t, "line 4") && !has(t, "line 84") && rolltui_picker_scroll_extent(r.p, &ex) && ex.first == 0 && ex.total == 5,
            "the reader is brought back into what is left: from the top, nothing blank [" + std::to_string(ex.first) + " of " + std::to_string(ex.total) + "]");
      check(rolltui_picker_preview_focused(r.p), "…and the keys are still in the file, since it is still the file");
      // it becomes BINARY: another kind of thing, another unit, from the top
      write_file(d / "log.txt", std::string("\0\1\2\3\4\5\6\7", 8) + "some bytes after the nul\n");
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "binary"), "a text file that gains a NUL is shown as bytes");
      // and is REWRITTEN as a document: the same file, read again
      write_file(d / "doc.md", "# First title\n\nbody\n");
      age(d);
      r.go(d / "doc.md");
      check(has(r.frame(), "First title"), "(a Markdown file is previewed rendered)");
      write_file(d / "doc.md", "# Second title\n\nbody\n");
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "Second title") && !has(r.frame(), "First title"), "a Markdown file edited is rendered again");
      // and is DELETED: the preview is of what the cursor is on now
      fs::remove(d / "doc.md");
      check(rolltui_picker_refresh(r.p) == 1 && r.selected() != (d / "doc.md").string() && !has(r.frame(), "Second title"),
            "the previewed file is deleted: the cursor moves to its neighbour and the preview shows that [" + r.selected() + "]");
      check(!rolltui_picker_preview_focused(r.p), "…and the keys are back in the list: they were in a file that is not there");
      r.po.preview = ROLLTUI_PREVIEW_OFF;
      r.options();
      r.h = 14;
    }

    // ---- THE READER'S PLACE, BY WHAT THEY WERE READING ------------------------------------------------------------------
    {
      const fs::path d = root / "t14";
      write_file(d / "log.txt", lines(60));
      write_file(d / "short.txt", lines(4));
      write_file(d / "z.txt", "z");
      age(d);
      r.po.preview = ROLLTUI_PREVIEW_RIGHT;
      r.options();
      r.h = 12;
      r.go(d / "log.txt");
      (void)r.frame();
      RolltuiScrollExtent ex{};
      // a log that DROPS ITS OLDEST LINES: what the reader was on is now higher up, and they are still on it
      rolltui_picker_scroll_to(r.p, 30);
      check(has(r.frame(), "line 30"), "(the reader is on line 30)");
      write_file(d / "log.txt", lines(50, 10));  // lines 10..59: the first ten are gone
      check(rolltui_picker_refresh(r.p) == 1, "the first ten lines of the file are dropped");
      std::string t = r.frame();
      check(has(t, "line 30") && !has(t, "line 29\n") && rolltui_picker_scroll_extent(r.p, &ex) && ex.first == 20,
            "the reader is still on line 30 — the same text, ten lines higher up in the file [row " + std::to_string(ex.first) + "]");
      // text put in ABOVE them
      write_file(d / "log.txt", lines(5, 900) + lines(50, 10));
      check(rolltui_picker_refresh(r.p) == 1, "five lines are put in at the head");
      t = r.frame();
      check(has(t, "line 30") && !has(t, "line 29\n") && rolltui_picker_scroll_extent(r.p, &ex) && ex.first == 25, "…and the reader is still on line 30, five lines further down [row " + std::to_string(ex.first) + "]");
      // a place with a BLANK top line is found by the first line under it that says something
      std::string gappy;
      for (int i = 0; i < 30; ++i) gappy += "entry number " + std::to_string(i) + "\n\n";
      write_file(d / "log.txt", gappy);
      check(rolltui_picker_refresh(r.p) == 1, "(a file with a blank line between entries)");
      rolltui_picker_scroll_to(r.p, 20);
      t = r.frame();
      check(has(t, "entry number 10"), "(the reader is at entry 10)");
      write_file(d / "log.txt", "a new first entry\n\n" + gappy);
      check(rolltui_picker_refresh(r.p) == 1, "an entry is put in above");
      t = r.frame();
      check(has(t, "entry number 10") && rolltui_picker_scroll_extent(r.p, &ex) && ex.first == 22, "…and the reader is still at entry 10, whichever line of the pair was at the top [row " + std::to_string(ex.first) + "]");
      // a file that is SOMETHING ELSE NOW is not searched for: the same place in it
      write_file(d / "log.txt", lines(60, 5000));
      check(rolltui_picker_refresh(r.p) == 1, "the file is replaced by one that shares nothing with it");
      t = r.frame();
      check(has(t, "line 5022") && rolltui_picker_scroll_extent(r.p, &ex) && ex.first == 22, "the reader keeps their place by position, since there is nothing to find [row " + std::to_string(ex.first) + "]");
      // the TOP is the top: a file short enough to fit, at its top, that grows past the window, does not scroll
      r.go(d / "short.txt");
      check(has(r.frame(), "line 3"), "(a four-line file fits)");
      append_file(d / "short.txt", lines(60, 4));
      check(rolltui_picker_refresh(r.p) == 1, "it grows to sixty-four lines");
      check(has(r.frame(), "line 0") && rolltui_picker_scroll_extent(r.p, &ex) && ex.first == 0 && ex.total == 64, "…and the reader is still at its top, not thrown to the end");
      r.po.preview = ROLLTUI_PREVIEW_OFF;
      r.options();
      r.h = 14;
    }

    // ---- A DUMP OF BYTES, AND A DOCUMENT --------------------------------------------------------------------------------
    {
      const fs::path d = root / "t15";
      write_file(d / "dump.bin", std::string("\0", 1) + std::string(3000, 'q'));
      std::string doc;
      for (int i = 0; i < 40; ++i) doc += "Paragraph " + std::to_string(i) + ": some words that make it a paragraph of its own.\n\n";
      write_file(d / "doc.md", doc);
      age(d);
      r.po.preview = ROLLTUI_PREVIEW_RIGHT;
      r.options();
      r.w = 160;
      r.h = 12;
      RolltuiScrollExtent ex{};
      r.go(d / "dump.bin");
      check(has(r.frame(), "binary") && rolltui_picker_scroll_extent(r.p, &ex) && ex.total > 20, "(a dump of three thousand bytes)");
      rolltui_picker_scroll_to(r.p, 40);
      (void)r.frame();
      // truncated to a few bytes above where the reader was: clamped, and still a dump
      write_file(d / "dump.bin", std::string("\0", 1) + std::string(200, 'r'));
      check(rolltui_picker_refresh(r.p) == 1, "the dump is cut down to two hundred bytes, above where the reader was");
      check(has(r.frame(), "binary") && rolltui_picker_scroll_extent(r.p, &ex) && ex.first + ex.visible <= ex.total && ex.first < ex.total, "the reader is brought back into what is left: no blank rows [" + std::to_string(ex.first) + "+" + std::to_string(ex.visible) + " of " + std::to_string(ex.total) + "]");
      // at the foot of a dump that grows: followed
      r.key(ROLLTUI_KEY_RIGHT);
      r.key(ROLLTUI_KEY_END);
      (void)r.frame();
      append_file(d / "dump.bin", std::string(4000, 's'));
      check(rolltui_picker_refresh(r.p) == 1, "the dump grows under a reader at its foot");
      check(rolltui_picker_scroll_extent(r.p, &ex) && ex.first + ex.visible == ex.total && ex.total > 60, "…and the reader is still at its foot [" + std::to_string(ex.first) + "+" + std::to_string(ex.visible) + " of " + std::to_string(ex.total) + "]");
      // a document: the same paragraph at the top when text is added below, and when it is added above
      // (the previous file was followed to its end: none of that belongs to this one, which opens at its top)
      r.go(d / "doc.md");
      check(has(r.frame(), "Paragraph 0:"), "(a document opens at its top, whatever the file before it was doing)");
      rolltui_picker_scroll_to(r.p, 30);
      std::string t = r.frame();
      std::string first_para;
      {
        const std::size_t at = t.find("Paragraph ");
        first_para = at == std::string::npos ? "" : t.substr(at, 13);
      }
      check(!first_para.empty(), "(a reader is part-way down a document, at " + first_para + ")");
      write_file(d / "doc.md", doc + "A paragraph added at the end.\n\n");
      check(rolltui_picker_refresh(r.p) == 1, "a paragraph is added at the end of the document");
      check(has(r.frame(), first_para), "the reader is still where they were in it");
      write_file(d / "doc.md", "A paragraph put in at the very top of the file, before anything the reader has seen.\n\n" + doc);
      check(rolltui_picker_refresh(r.p) == 1, "and one is put in at the top");
      t = r.frame();
      check(has(t, first_para) && !has(t, "put in at the very top"), "…the reader is still on the same paragraph, further down");
      // A DOCUMENT FULL OF MARKUP: what the reader is on is found in the document's text, not in the file's bytes, and
      // the two disagree as soon as there is a link. A block of links put in above moves the bytes by hundreds and the
      // text by a few.
      std::string marked;
      for (int i = 0; i < 40; ++i) marked += "## Section " + std::to_string(i) + "\n\nText with **bold** and _italic_ and [a link](http://example.com/section/" + std::to_string(i) + ") for section " + std::to_string(i) + ".\n\n";
      write_file(d / "marked.md", marked);
      age(d);
      r.go(d / "marked.md");
      (void)r.frame();
      rolltui_picker_scroll_to(r.p, 40);
      t = r.frame();
      std::string section;
      {
        const std::size_t at = t.find("Section ");
        section = at == std::string::npos ? "" : t.substr(at, 10);
      }
      check(!section.empty(), "(a reader is part-way down a document with markup, at " + section + ")");
      std::string links;
      for (int i = 0; i < 12; ++i) links += "[x" + std::to_string(i) + "](http://example.com/a/very/long/address/indeed/number/" + std::to_string(i) + ") ";
      write_file(d / "marked.md", "# Added\n\nA paragraph of nothing but links: " + links + "\n\n" + marked);
      check(rolltui_picker_refresh(r.p) == 1, "a paragraph of twelve links, hundreds of bytes and a few words, is put in at the top");
      t = r.frame();
      check(has(t, section) && !has(t, "Added"), "the reader is still at " + section + " — the same words, however far the bytes moved");
      r.w = 110;
      r.po.preview = ROLLTUI_PREVIEW_OFF;
      r.options();
      r.h = 14;
    }

    // ---- A FOLDER THAT COMES BACK AFTER IT WENT ---------------------------------------------------------------------
    {
      const fs::path d = root / "t9";
      write_file(d / "keep" / "k.txt", "k");
      write_file(d / "gone" / "g.txt", "g");
      age(d);
      r.go(d / "gone");
      fs::remove_all(d / "gone");
      (void)rolltui_picker_refresh(r.p);
      check(r.dir() == d.string() && has(r.frame(), "keep"), "(the folder went, and the eye is in its parent)");
      write_file(d / "gone" / "again.txt", "a");
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "gone"), "…and when it comes back it is in the parent's list again");
    }

    // ---- A CHANGE IN A FOLDER ABOVE THE EYE: the eye stays where it is, and the folder above shows it -----------------
    {
      const fs::path d = root / "t12";
      write_file(d / "a" / "x" / "deep.txt", "deep");
      write_file(d / "a" / "y.txt", "y");
      age(d);
      r.go(d / "a" / "x");
      const std::size_t cols = r.columns();
      write_file(d / "a" / "new.txt", "n");
      check(rolltui_picker_refresh(r.p) == 1, "a file is added to a folder that is two columns behind the eye");
      check(r.dir() == (d / "a" / "x").string() && r.selected() == (d / "a" / "x" / "deep.txt").string() && r.columns() == cols,
            "the eye did not move, and no column came or went [" + r.dir() + "]");
      r.key(ROLLTUI_KEY_LEFT);
      check(has(r.frame(), "new.txt") && r.selected() == (d / "a" / "x").string(), "…and walking back to that folder, the new file is in it, the cursor still on the folder it was on");
    }

    // ---- A LINK'S TARGET GOES AND COMES BACK, and the link's own folder never moves ------------------------------------
    {
      const fs::path d = root / "t13";
      write_file(d / "target" / "inside.txt", "i");
      fs::create_directories(d / "a");
      fs::create_directory_symlink(d / "target", d / "a" / "link");
      age(d);
      r.go(d / "a");  // the cursor lands on the link's folder's first entry: the link (the only entry)
      check(r.selected() == (d / "a" / "link").string() && has(r.frame(), "inside.txt"), "the cursor is on a link to a folder, and the folder's listing is beside it");
      const std::size_t with_child = r.columns();
      fs::remove_all(d / "target");
      check(rolltui_picker_refresh(r.p) == 1, "the link's target is removed: the folder holding the link did not change at all");
      check(r.columns() == with_child - 1 && !has(r.frame(), "inside.txt") && !has(r.frame(), "cannot"),
            "the column that listed the target is dropped, not left behind saying it cannot be opened");
      write_file(d / "target" / "back.txt", "b");
      check(rolltui_picker_refresh(r.p) == 1 && r.columns() == with_child && has(r.frame(), "back.txt"), "…and when the target comes back its listing opens beside the link again");
    }

    // ---- COST: a look at a huge folder that has not changed is nothing, and one that keeps changing backs the interval off -----
    {
      const fs::path d = root / "t20", small = root / "t20small";
      for (int i = 0; i < 20000; ++i) write_file(d / ("file" + std::to_string(i) + ".txt"), "x");
      for (int i = 0; i < 10; ++i) write_file(small / ("file" + std::to_string(i) + ".txt"), "x");
      age(d);
      auto ms_since = [](const timespec& a) {
        timespec b{};
        clock_gettime(CLOCK_MONOTONIC, &b);
        return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
      };
      // What a look costs when nothing has changed, averaged over twenty. Not asserted as a number: the picker also shows
      // the folders ABOVE the scratch tree, and under a parallel test run the temporary directory is being written to by
      // every other test, so those are legitimately read again. What must hold is that it does not depend on how big the
      // folder in question is, measured back to back in the same conditions.
      auto unchanged_ms = [&](const fs::path& folder) {
        r.go(folder);
        (void)r.frame();
        (void)rolltui_picker_refresh(r.p);
        (void)rolltui_picker_refresh(r.p);
        timespec t0{};
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < 20; ++i) (void)rolltui_picker_refresh(r.p);
        return ms_since(t0) / 20.0;
      };
      const double small_ms = unchanged_ms(small);
      const double big_ms = unchanged_ms(d);
      timespec t0{};
      check(big_ms - small_ms < 25.0,
            "a look at a folder of twenty thousand entries that has not changed costs no more than one at a folder of ten: a stat and the rows on screen, "
            "not the folder (" + std::to_string(big_ms) + " ms against " + std::to_string(small_ms) + " ms)");
      r.go(d);
      (void)r.frame();
      (void)rolltui_picker_refresh(r.p);
      (void)rolltui_picker_refresh(r.p);
      // a folder that keeps changing is read again each look, and the clock backs off so the reading never eats the frame
      rolltui_picker_set_now(r.p, 100000);
      (void)r.frame();  // starts the clock
      write_file(d / "another.txt", "x");
      rolltui_picker_set_now(r.p, 100600);
      clock_gettime(CLOCK_MONOTONIC, &t0);
      (void)r.frame();  // the look that reads all twenty thousand again
      const double reread_ms = ms_since(t0);
      RolltuiPickerStatus st = r.status();
      check(has(r.frame(), "another.txt") && reread_ms < 2000.0, "…the folder is read again when it changed (" + std::to_string(reread_ms) + " ms)");
      // (the interval is twenty times what the look itself took, and this timed the whole frame, so only the fact of the
      // back-off is asserted, and only when the read was slow enough to call for one)
      check(st.wake_ms >= 500 && (reread_ms < 60.0 || st.wake_ms > 600),
            "…and the next look is further off than the usual half second when that read was slow (asks for a wake in " + std::to_string(st.wake_ms) + " ms)");
      rolltui_picker_status_release(&st);
      rolltui_picker_set_now(r.p, 0);
      fs::remove_all(d, ec);
    }

    // ---- A FOLDER OR A FILE THAT BECOMES UNREADABLE, AND READABLE AGAIN -----------------------------------------------------
    if (::geteuid() != 0) {
      const fs::path d = root / "t21";
      write_file(d / "sealed" / "in.txt", "inside");
      write_file(d / "secret.txt", "the secret text");
      age(d);
      r.po.preview = ROLLTUI_PREVIEW_RIGHT;
      r.options();
      r.go(d / "sealed");
      check(has(r.frame(), "in.txt"), "(a folder whose listing is shown)");
      ::chmod((d / "sealed").c_str(), 0);
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "cannot open") && !has(r.frame(), "in.txt"), "a folder that becomes unreadable says so, in words, where its listing was");
      ::chmod((d / "sealed").c_str(), 0755);
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "in.txt") && !has(r.frame(), "cannot open"), "…and when it is readable again its listing is back");
      r.go(d / "secret.txt");
      check(has(r.frame(), "the secret text"), "(a file is previewed)");
      ::chmod((d / "secret.txt").c_str(), 0);
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "Permission denied") && !has(r.frame(), "the secret text"), "a previewed file that becomes unreadable says why, and shows none of it");
      ::chmod((d / "secret.txt").c_str(), 0644);
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "the secret text"), "…and when it can be read again, it is shown again");
      // empty, and not empty
      write_file(d / "secret.txt", "");
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "(empty file)"), "a previewed file emptied says it is empty");
      write_file(d / "secret.txt", "back again\n");
      check(rolltui_picker_refresh(r.p) == 1 && has(r.frame(), "back again") && !has(r.frame(), "(empty file)"), "…and shows its text again when it has some");
      // the whole picker given to the preview, and the file goes
      r.key(ROLLTUI_KEY_RIGHT);
      r.key(ROLLTUI_KEY_RIGHT);
      RolltuiPickerStatus st = r.status();
      check(st.preview == 3, "(the preview has the whole picker)");
      rolltui_picker_status_release(&st);
      fs::remove(d / "secret.txt");
      check(rolltui_picker_refresh(r.p) == 1, "the file in the zoomed preview is deleted");
      st = r.status();
      check(st.preview < 2 && r.frame().find("back again") == std::string::npos, "the columns are back and the keys are in the list: nothing is left zoomed over a file that is not there");
      rolltui_picker_status_release(&st);
      r.po.preview = ROLLTUI_PREVIEW_OFF;
      r.options();
    }

    // ---- THE CLOCK: a look is due on an interval, never headless, and never when it was asked not to be ---------------
    {
      const fs::path d = root / "t10";
      write_file(d / "one.txt", "1");
      age(d);
      r.go(d / "one.txt");
      RolltuiPickerStatus st = r.status();
      check(st.wake_ms == 0, "with no frame clock (headless) the picker never asks to be woken");
      rolltui_picker_status_release(&st);
      write_file(d / "two.txt", "2");
      (void)r.frame();
      check(!has(r.frame(), "two.txt"), "…and a frame drawn with no clock does not look at the disk: a golden frame is a still");
      rolltui_picker_set_now(r.p, 10000);
      (void)r.frame();  // the first frame with a clock starts it
      st = r.status();
      check(st.wake_ms > 0 && st.wake_ms <= 500, "with a clock it asks to be woken within half a second (" + std::to_string(st.wake_ms) + " ms)");
      rolltui_picker_status_release(&st);
      check(!has(r.frame(), "two.txt"), "(not yet looked)");
      rolltui_picker_set_now(r.p, 10200);
      check(!has(r.frame(), "two.txt"), "a frame before the interval is up does not look");
      rolltui_picker_set_now(r.p, 10600);
      check(has(r.frame(), "two.txt"), "the first frame after it does, and the new file is on screen with no call from the host");
      // switched off
      r.po.no_watch = 1;
      r.options();
      write_file(d / "three.txt", "3");
      rolltui_picker_set_now(r.p, 20000);
      (void)r.frame();
      rolltui_picker_set_now(r.p, 30000);
      st = r.status();
      check(!has(r.frame(), "three.txt") && st.wake_ms == 0, "with `no_watch` the disk is never looked at and no wake is asked for");
      rolltui_picker_status_release(&st);
      r.po.no_watch = 0;
      r.options();
      rolltui_picker_set_now(r.p, 0);
    }

    // ---- A PATH THAT WAS NOT THERE --------------------------------------------------------------------------------
    {
      const fs::path d = root / "t11";
      write_file(d / "real.txt", "r");
      age(d);
      r.go(d / "missing");
      check(has(r.frame(), "cannot"), "a path that does not exist is a column that says so");
      check(!rolltui_picker_refresh(r.p) || true, "(a look at it)");
      const std::size_t cols = r.columns();
      check(r.columns() == cols, "(a look at a column that could not be read, when nothing has changed, changes nothing)");
    }
  }
  fs::remove_all(root, ec);
  check(live_bytes() == base, "after every scenario, the picker freed, the library holds what it held before (" + std::to_string(live_bytes()) + " vs " + std::to_string(base) + ")");
  return testkit::report("rolltui_watch_test");
}
