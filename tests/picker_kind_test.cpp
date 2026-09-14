// rolltui/tests/picker_kind_test.cpp — an app gets a file picker by NAMING it in a layout.
//
// The claim is the same one the theme and keys editors make and it is the reason the mechanism
// left the explorer: a capability every app needs is the library's, and a host reaches it through
// a layout file plus, here, two calls — where to start, and what was chosen.
//
// The fixture screen is `quarry`, and the word appears in no source this binary is built from.
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <string>

// ONLY the public header, deliberately: this suite is a CONSUMER-shaped program, so what it can
// reach is what a host can reach. A test that opts into an internal header proves nothing about
// the surface.
#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

namespace {
namespace fs = std::filesystem;

void write_file(const fs::path& p, const std::string& t) {
  const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) { (void)!::write(fd, t.data(), t.size()); ::close(fd); }
}

std::string read_all(const fs::path& p) {
  std::string out;
  char buf[4096];
  const int fd = ::open(p.c_str(), O_RDONLY);
  if (fd < 0) return out;
  for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0;) out.append(buf, (std::size_t)n);
  ::close(fd);
  return out;
}

RolltuiEvent make_key(unsigned char k) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = k;
  return e;
}

// A HELD event, not a temporary: `rolltui_windows_handle` takes a pointer and the address of a
// temporary dies at the end of the full expression.
RolltuiEvent g_key;
const RolltuiEvent* press(unsigned char k) {
  g_key = make_key(k);
  return &g_key;
}

}  // namespace

int main() {
  using testkit::check;
  const std::string tmp = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
  const fs::path root = fs::path(tmp) / ("rolltui_picker_test_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "inner");
  write_file(root / "chosen.txt", "the answer");
  write_file(root / "inner" / "deeper.txt", "further in");

  RolltuiContext* ctx = rolltui_context_new();
  rolltui_context_set_library_defaults(ctx);
  RolltuiWindows* windows = rolltui_windows_new(ctx);
  RolltuiWindowStack* stack = rolltui_window_stack_new();

  // ---- the screen is a FILE, and the app names nothing ---------------------------------------
  const std::string text = read_all(fs::path(ROLLTUI_FIXTURE_DIR) / "quarry" / "quarry.json");
  RolltuiLayoutReport rep{};
  RolltuiLayout* layout = rolltui_load_layout_text(text.data(), text.size(), nullptr, 0, nullptr, &rep);
  check(layout != nullptr && rolltui_layout_report_clean(&rep),
        "the screen loads from its file with nothing to report");
  rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));

  RolltuiWidgetEnv env{0, 0};
  rolltui_context_set_env(ctx, &env);
  const RolltuiRect area{0, 0, 60, 14};
  rolltui_windows_sync(windows, stack);
  rolltui_windows_autosize(windows, stack, area);
  rolltui_windows_layout(windows, stack, area);

  check(rolltui_windows_report_count(windows) == 0,
        "…and the app provides everything it names: `filepicker` is the library's, not this test's");

  // NOTE the two different keys: a window is handled by its ID (), and a kind is reached
  // by its CONTENT () — the same string the layout wrote after the colon-less name.
  // THE KEYS ARE THE BINDINGS FILE'S: the `picker` scope is the library's, so the shipped table
  // already names them, and a windows table resolves them through its context.
  rolltui_context_set_bindings(ctx, rolltui_bindings_default(ctx));
  // ---- where it looks -----------------------------------------------------------------------
  const std::string dir = root.string();
  rolltui_windows_set_picker_dir(windows, "filepicker", 10, dir.data(), dir.size());
  RolltuiStr got{};
  check(rolltui_windows_picker_taken(windows, "filepicker", 10, &got) == 0,
        "nothing is taken before anything is chosen");
  // ---- the columns: the cursor opens on the first entry, folders first ----------------------
  // `inner` sorts before `chosen.txt` because directories come first, which is the shared
  // mechanism's rule and not this widget's.
  int is_dir = 0;
  check(rolltui_windows_picker_selected(windows, "filepicker", 10, &got, &is_dir) == 1 &&
            std::string(got.p ? got.p : "", got.n) == (root / "inner").string() && is_dir == 1,
        "the cursor opens on the first entry, a folder, and the host can ask what is under it [" + std::string(got.p ? got.p : "", got.n) + "]");
  check(rolltui_windows_picker_dir(windows, "filepicker", 10, &got) == 1 && std::string(got.p ? got.p : "", got.n) == dir,
        "…and where the cursor is: the directory a save dialog would join a name onto");
  // ---- Enter on a DIRECTORY enters it, by default; Enter on a file takes it ----------------
  rolltui_windows_handle(windows, "pick", 4, press(ROLLTUI_KEY_ENTER));  // into inner/
  check(rolltui_windows_picker_taken(windows, "filepicker", 10, &got) == 0,
        "Enter on a DIRECTORY walks into it rather than answering with it");
  check(rolltui_windows_picker_dir(windows, "filepicker", 10, &got) == 1 && std::string(got.p ? got.p : "", got.n) == (root / "inner").string(),
        "…the focus is inside it");
  rolltui_windows_handle(windows, "pick", 4, press(ROLLTUI_KEY_ENTER));  // deeper.txt, its only entry
  check(rolltui_windows_picker_taken(windows, "filepicker", 10, &got) == 1 &&
            std::string(got.p ? got.p : "", got.n) == (root / "inner" / "deeper.txt").string(),
        "…and the file inside it answers with the deeper path [" + std::string(got.p ? got.p : "", got.n) + "]");
  check(rolltui_windows_picker_taken(windows, "filepicker", 10, &got) == 0,
        "…collected ONCE, so a host asking every frame cannot act on one choice twice");
  // ---- and back out: Left is the way up, and the cursor is where it was ----------------------
  rolltui_windows_handle(windows, "pick", 4, press(ROLLTUI_KEY_LEFT));
  rolltui_windows_handle(windows, "pick", 4, press(ROLLTUI_KEY_DOWN));  // chosen.txt
  rolltui_windows_handle(windows, "pick", 4, press(ROLLTUI_KEY_ENTER));
  check(rolltui_windows_picker_taken(windows, "filepicker", 10, &got) == 1 &&
            std::string(got.p ? got.p : "", got.n) == (root / "chosen.txt").string(),
        "Left climbs back out and Down, Enter chooses the file beside the folder [" + std::string(got.p ? got.p : "", got.n) + "]");
  // ---- a directory picker: Enter TAKES a folder when the host says so ------------------------
  RolltuiPickerOptions opts{};
  rolltui_picker_options_init(&opts);
  opts.take_folders = 1;
  rolltui_windows_set_picker_options(windows, "filepicker", 10, &opts);
  rolltui_windows_handle(windows, "pick", 4, press(ROLLTUI_KEY_UP));  // inner/
  rolltui_windows_handle(windows, "pick", 4, press(ROLLTUI_KEY_ENTER));
  check(rolltui_windows_picker_taken(windows, "filepicker", 10, &got) == 1 &&
            std::string(got.p ? got.p : "", got.n) == (root / "inner").string(),
        "with `take_folders`, Enter on a folder answers with it — a directory picker [" + std::string(got.p ? got.p : "", got.n) + "]");
  // ---- the fuller answer: a cancel and a copy are events too -------------------------------
  rolltui_windows_handle(windows, "pick", 4, press(ROLLTUI_KEY_ESCAPE));
  RolltuiPickerEvent ev{};
  check(rolltui_windows_picker_event(windows, "filepicker", 10, &ev) == 1 && ev.kind == ROLLTUI_PICKER_EVENT_CANCELLED,
        "Escape is a CANCELLED event, for the host to act on");
  rolltui_picker_event_release(&ev);
  check(rolltui_windows_picker_taken(windows, "filepicker", 10, &got) == 0,
        "…which `_taken`, the narrow form, drops: a dialog that wants only the path never sees it");
  // ---- a start the LAYOUT states, with no host call ------------------------------------------
  {
    const std::string sub = (root / "inner").string();
    const std::string screen = std::string("{\"name\":\"quarry2\",\"min_width\":0,\"min_height\":0,\"actions\":{},\"root\":{\"row\":[") +
        "{\"id\":\"pick2\",\"content\":\"filepicker:" + sub + "\",\"border\":\"single\",\"title\":\"pick\",\"focusable\":true}]}}";
    RolltuiLayoutReport rep2{};
    RolltuiLayout* l2 = rolltui_load_layout_text(screen.data(), screen.size(), nullptr, 0, nullptr, &rep2);
    check(l2 != nullptr && rolltui_layout_report_clean(&rep2), "a layout may state where the picker starts, as its source");
    rolltui_window_stack_set_base(stack, rolltui_layout_base(l2));
    rolltui_windows_sync(windows, stack);
    rolltui_windows_autosize(windows, stack, area);
    rolltui_windows_layout(windows, stack, area);
    const std::string content = "filepicker:" + sub;
    check(rolltui_windows_picker_dir(windows, content.data(), content.size(), &got) == 1 && std::string(got.p ? got.p : "", got.n) == sub,
          "…and it opens there, with no host code naming the path [" + std::string(got.p ? got.p : "", got.n) + "]");
    rolltui_layout_report_release(&rep2);
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    rolltui_layout_free(l2);
  }
  rolltui_str_free(&got);
  rolltui_layout_report_release(&rep);
  rolltui_window_stack_free(stack);
  rolltui_windows_free(windows);
  rolltui_layout_free(layout);
  rolltui_context_free(ctx);
  fs::remove_all(root, ec);
  return testkit::report("picker_kind_test");
}
