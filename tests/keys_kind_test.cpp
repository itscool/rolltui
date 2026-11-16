//
// keys_kind_test.cpp — THE PROOF: an app gets a keys editor by NAMING it in a layout.
//
// `keys` is one of the library's own widget kinds, resolved through the same closed-table-then-
// host rungs as `menu` and `theme`. So a screen that wants a keys editor says so in a file, and
// the binary that draws it needs no editor code — which is the whole difference between an
// editor one program owns and an editor the library does.
//
// Everything the screen is lives in rolltui/tests/fixtures/anvil/, copied into a scratch preset
// directory before the REAL rolltui-studio binary is run against it:
//
//   layouts/anvil.json        the design — a text: window and a `keys` window, the two editor
//                             actions it declares, and `app.quench`, which no binary knows
//   layouts/anvil-plain.json  the SAME screen with that one window changed to a text: one
//   bindings/anvil.json       the keys — the only thing that gives editor.undo a chord, and
//                             deliberately not the chord the studio's own tools suggest
//
// THE CONTROLS ARE WHAT CARRY THIS, in the shape theme_kind_test established:
//
//   THE SOURCE CONTROL. The word "anvil" — the layouts, the bindings file, both window ids —
//   appears in no source the binary is built from. Nothing special-cases this screen.
//
//   THE ONE-WINDOW CONTROL. anvil-plain is the same screen with `keys` replaced by `text:`.
//   Every assertion about the editor flips, so it is the CONTENT STRING and not the studio that
//   puts an editor there.
//
//   THE BINDINGS CONTROL. Ctrl-O is `transcript.fold` in the shipped table and `editor.undo`
//   only in anvil's own file. The same keystrokes with the shipped bindings leave the edit
//   standing, so the undo in the run above came from the FILE.
//
//   THE LIVE-TABLE CONTROL. `app.quench` is declared by the layout FILE and by nothing else.
//   It is in the editor's tree, which is only possible if the table being edited is the one the
//   screen is running on — a preset file parsed off disk carries rows and no declarations, and
//   an action nothing declares is not editable.
//
//   THE STORE CONTROL (in process, below). A screen with a `keys` window changes nothing
//   durable by itself: where a rebinding LANDS is the app's, so a commit goes where the app
//   said and nowhere else. Handed a store, a commit is in it and its version moves; handed
//   none, the same keystrokes leave it untouched.
//
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;

#ifndef ROLLTUI_STUDIO_BIN
#error "ROLLTUI_STUDIO_BIN must name the studio binary"
#endif
#ifndef ROLLTUI_FIXTURE_DIR
#error "ROLLTUI_FIXTURE_DIR must point at rolltui/tests/fixtures"
#endif
#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui/"
#endif

namespace fs = std::filesystem;

namespace {

std::string run(const std::string& cmd, int& rc) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) { rc = -1; return out; }
  char buf[4096];
  std::size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
  rc = pclose(p);
  return out;
}

bool has(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

RolltuiEvent key_event(unsigned char k, RolltuiCodepoint ch = 0, bool alt = false, bool ctrl = false) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = k;
  e.key.ch = ch;
  e.key.alt = alt ? 1 : 0;
  e.key.ctrl = ctrl ? 1 : 0;
  return e;
}
RolltuiEvent ctrl_key(char c) { return key_event(ROLLTUI_KEY_CHAR, static_cast<RolltuiCodepoint>(c), false, true); }

// The one screen the in-process half drives: a `keys` window and nothing else, written here
// rather than read from a file because what is under test is the KIND, not the loader.
const char* kInProcessLayout = R"({
  "name": "anvil-inproc", "min_width": 0, "min_height": 0, "focus": "anvil_edit",
  "actions": {},
  "root": { "id": "anvil_edit", "content": "keys", "focusable": true }
})";

// The one string an assertion can compare: "app.quench" when quench's row holds Alt-J, else empty. A row, not a declared
// action: the store's copy is the person's file, and the screen's declarations are laid over it when the app takes it.
std::string alt_j_holder(const RolltuiBindings* b) {
  const std::size_t n = rolltui_bindings_chord_count(b, "app.quench", 10);
  for (std::size_t i = 0; i < n; ++i) {
    RolltuiChord k{};
    if (rolltui_bindings_chord_at(b, "app.quench", 10, i, &k) && k.key == ROLLTUI_KEY_CHAR && k.ch == 'j' && k.alt && !k.ctrl)
      return "app.quench";
  }
  return std::string();
}

// A screen that names `keys`, with the store handed over (or not), held open so a caller can
// send its own keystrokes and read the store back.
struct Screen {
  RolltuiContext* ctx = nullptr;
  RolltuiWindows* windows = nullptr;
  RolltuiWindowStack* stack = nullptr;
  RolltuiBindings* bindings = nullptr;
  RolltuiLayout* layout = nullptr;
  RolltuiBindingsStore* store = nullptr;

  void open(const std::string& dir, bool hand_over_the_store) {
    ctx = rolltui_context_new();
    rolltui_context_set_library_defaults(ctx);
    rolltui_context_set_dir(ctx, dir.data(), dir.size());
    store = rolltui_bindings_store_new(ctx, dir.data(), dir.size(), 0, "", 0);
    RolltuiBindingsPresetReport start{};
    rolltui_bindings_store_start(store, &start);
    rolltui_bindings_preset_report_release(&start);

    RolltuiLayoutReport lrep{};
    layout = rolltui_load_layout_text(kInProcessLayout, std::strlen(kInProcessLayout), nullptr, 0, nullptr, &lrep);
    rolltui_layout_report_release(&lrep);

    windows = rolltui_windows_new(ctx);
    stack = rolltui_window_stack_new();
    // A CLONE, not the store's own borrow: `ctx`'s bindings are what routes keys TODAY, and the
    // widget kind commits into the store separately — a check below asserts the two stay apart
    // until a Save closes the gap.
    bindings = rolltui_bindings_clone(rolltui_bindings_store_working(store));
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));

    RolltuiWidgetEnv env{};
    rolltui_context_set_env(ctx, &env);
    rolltui_context_set_bindings(ctx, bindings);
    rolltui_windows_sync(windows, stack);
    rolltui_windows_layout(windows, stack, RolltuiRect{0, 0, 80, 30});

    // WHAT THE APP DECLARES, in the order a host does it: the windows exist first, so the
    // `keys` widget was built before this line ran. `app.quench` is in no file this store can
    // read, so an editor that offers it took the table again after the declaration arrived.
    RolltuiLayoutAction declared{};
    rolltui_str_set(&declared.name, "app.quench", 10);
    rolltui_str_set(&declared.description, "an action this screen declares and no file carries", 50);
    rolltui_bindings_declare(bindings, &declared, 1, nullptr, 0);

    // THE ONE LINE AN APP WRITES. Everything after it is the editor driving itself.
    if (hand_over_the_store) rolltui_windows_set_bindings_store(windows, "keys", 4, store, /*persist=*/0);
  }

  void send(const RolltuiEvent& e) { rolltui_windows_handle(windows, "anvil_edit", 10, &e); }
  void type(const char* text) {
    for (const char* p = text; *p; ++p) send(key_event(ROLLTUI_KEY_CHAR, static_cast<RolltuiCodepoint>(*p)));
  }

  ~Screen() {
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_layout_free(layout);
    rolltui_bindings_free(bindings);
    rolltui_bindings_store_free(store);
    rolltui_context_free(ctx);
  }
};

struct Session {
  unsigned long long version_before = 0, version_after = 0;
  std::string holder_before, holder_after;
};

// One editing session over a screen that names `keys`: Alt-J onto `app.quench`, through the
// sheet's own filter so the walk does not depend on where a row happens to sit. The action is
// the APP's declaration and nothing else's, so a session that can reach it is one editing the
// table the app is running on.
Session drive(const std::string& dir, bool hand_over_the_store) {
  Session out;
  Screen s;
  s.open(dir, hand_over_the_store);

  auto snapshot = [&](std::string& holder) {
    const RolltuiBindings* v = rolltui_bindings_store_working(s.store);
    if (v) holder = alt_j_holder(v);
  };
  out.version_before = rolltui_bindings_store_version(s.store);
  snapshot(out.holder_before);

  const RolltuiEvent enter = key_event(ROLLTUI_KEY_ENTER);
  s.type("/no file carries");  // the filter, to quench's row: its description
  s.send(enter);                // the filter kept, the cursor on the row
  s.send(enter);                // the row waits for a key…
  s.send(key_event(ROLLTUI_KEY_CHAR, 'j', /*alt=*/true));

  out.version_after = rolltui_bindings_store_version(s.store);
  snapshot(out.holder_after);
  return out;
}

}  // namespace

int main() {
  const std::string studio = ROLLTUI_STUDIO_BIN;
  const fs::path scratch = fs::temp_directory_path() / ("rolltui-anvil-" + std::to_string(getpid()));
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  fs::copy(fs::path(ROLLTUI_FIXTURE_DIR) / "anvil", scratch, fs::copy_options::recursive);
  const std::string presets = " --presets '" + scratch.string() + "'";
  const std::string base = studio + " --frame 100x30" + presets + " --theme default-dark";
  // `Type:` reads an underscore as a space.
  const std::string to_word_left = "/ Type:move_one_word_left Enter Enter";

  // ---- 1. A LAYOUT ALONE GIVES A WORKING SHEET ----------------------------------------------
  int rc = 0;
  const std::string open = run(base + " --layout anvil --bindings anvil 2>&1", rc);
  check(rc == 0 && !open.empty(), "the studio runs a screen it has never heard of (rc " + std::to_string(rc) + ")");
  check(has(open, "the keys") && has(open, "This program \xE2\x94\x80") && has(open, "cycle the shipped theme presets"),
        "…and the window whose content is `keys` draws the sheet: every key, in words, under its heading, the program's first");
  check(has(open, "Enter add a key") && has(open, "Ctrl-O undo") && has(open, " anvil \xE2\x94\x80"),
        "…with its own keys along the bottom, as this table binds them, and the store's preset in the footer");

  // ---- 2. IT IS DRIVABLE, with no host code between the keys and the model -------------------
  const std::string edited = run(base + " --layout anvil --bindings anvil --keys \"" + to_word_left + " AltJ\" 2>&1", rc);
  check(has(edited, "bound Alt-J \xE2\x86\x92 move one word left"),
        "keystrokes reach the sheet: Enter on a row takes the next key pressed as the row's new key");
  check(has(edited, "anvil (modified)"), "…committed into the studio's store, so the preset is labelled modified");

  // ---- 3. THE LIVE-TABLE CONTROL ------------------------------------------------------------
  //
  // What makes an action editable is that something DECLARED it, and the only declaration of
  // `app.quench` in this process is the layout file. Its presence proves the table being edited
  // is the one the screen is running on rather than a preset parsed off disk.
  const std::string app_scope = run(base + " --layout anvil --bindings anvil --keys \"/ Type:no_binary_knows Enter\" 2>&1", rc);
  check(has(app_scope, "app.quench: an action this screen declares") && has(app_scope, "no key"),
        "the sheet carries an action declared by the LAYOUT and by no source, so the table is the live one");
  const std::string own_screen = run(base + " --bindings anvil --keys \"F7 Enter Type:app Enter\" 2>&1", rc);
  check(!has(own_screen, "quench"),
        "…and the studio's own screen, which declares no such action, has none: the layout supplied it");

  // ---- 4. THE ONE-WINDOW CONTROL ------------------------------------------------------------
  const std::string plain = run(base + " --layout anvil-plain --bindings anvil 2>&1", rc);
  check(rc == 0 && !plain.empty() && !has(plain, "add a key") && !has(plain, "Typing \xE2\x94\x80"),
        "the SAME screen with that one content string changed has no sheet in it");

  // ---- 5. THE BINDINGS CONTROL --------------------------------------------------------------
  const std::string undone =
      run(base + " --layout anvil --bindings anvil --keys \"" + to_word_left + " AltJ CtrlO\" 2>&1", rc);
  check(has(undone, "undone") && !has(undone, "(modified)"),
        "Ctrl-O undoes, because anvil's bindings file says sheet.undo is Ctrl-O, and the preset is as it was again");
  const std::string shipped = run(base + " --layout anvil --keys \"" + to_word_left + " AltJ CtrlO\" 2>&1", rc);
  check(!has(shipped, "undone") && has(shipped, "(modified)"),
        "…and with the shipped bindings the same key does not undo: the FILE supplied the chord");

  // ---- 6. THE SOURCE CONTROL ----------------------------------------------------------------
  {
    std::vector<std::string> namers;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
      const std::string path = e.path().string();
      const std::string ext = e.path().extension().string();
      if (ext != ".h" && ext != ".hpp" && ext != ".c" && ext != ".cpp") continue;
      if (path.find("/tests/") != std::string::npos || path.find("/third_party/") != std::string::npos) continue;
      if (has(read_file(path), "anvil")) namers.push_back(path);
    }
    check(namers.empty(),
          "no source the studio is built from names this screen" + (namers.empty() ? "" : " — " + namers.front()));
    check(has(read_file(std::string(ROLLTUI_FIXTURE_DIR) + "/anvil/layouts/anvil.json"), "anvil"),
          "…and the matcher does find the word where it belongs, so the absence above is real");
  }

  // ---- 7. WHERE THE REBINDING LANDS ---------------------------------------------------------
  {
    const fs::path a = scratch / "store-given";
    const fs::path b = scratch / "store-withheld";
    fs::create_directories(a);
    fs::create_directories(b);
    const Session given = drive(a.string(), true);
    const Session withheld = drive(b.string(), false);
    check(given.holder_before.empty() && withheld.holder_before.empty(),
          "both runs start from a table where Alt-J is free, so what lands is the edit");
    check(given.version_after != given.version_before && given.holder_after == "app.quench",
          "handed the app's bindings store, the commit is IN it: the version moved and Alt-J is quench's [" +
              given.holder_after + "]");
    check(withheld.version_after == withheld.version_before && withheld.holder_after.empty(),
          "…and withheld, the same keystrokes leave it untouched: where a rebinding lands is the APP's");
  }

  // ---- 8. A SAVE NEVER SILENTLY REPLACES SOMEONE ELSE'S PRESET ------------------------------
  //
  // The store offers to refuse a name already taken, and a widget has nowhere to ask, so the
  // kind takes the refusal and says so. Overwriting a preset because a name was reused is a data
  // loss with no undo behind it — so the assertion is on what is IN the file after a second save
  // of the same name, not on whether one exists.
  {
    const fs::path dir = scratch / "save-as";
    fs::create_directories(dir);
    Screen s;
    s.open(dir.string(), true);
    const RolltuiEvent enter = key_event(ROLLTUI_KEY_ENTER);
    const RolltuiEvent escape = key_event(ROLLTUI_KEY_ESCAPE);
    auto back = [&]() { return rolltui_windows_back(s.windows, "anvil_edit", 10); };

    // Save as is a key on the sheet (Ctrl-S): it asks for a name on the bottom line, and Enter saves.
    auto save_as = [&](const char* name) {
      while (back() == 1) {
      }
      s.send(ctrl_key('s'));
      s.type(name);
      s.send(enter);
    };
    auto preset_holder = [&](const char* name) {
      RolltuiBindingsPresetReport rep{};
      RolltuiBindings* v = rolltui_bindings_store_get(s.store, name, std::strlen(name), &rep);
      std::string holder = "(no preset)";
      if (v) {
        holder = alt_j_holder(v);
        rolltui_bindings_free(v);
      }
      rolltui_bindings_preset_report_release(&rep);
      return holder;
    };

    save_as("mine");
    check(preset_holder("mine").empty(),
          "Ctrl-S saves the keys as a preset, through the store the app handed over [" + preset_holder("mine") + "]");

    // Now rebind something and try the same name again.
    while (back() == 1) {
    }
    s.type("/no file carries");
    s.send(enter);
    s.send(enter);
    s.send(key_event(ROLLTUI_KEY_CHAR, 'j', /*alt=*/true));
    check(alt_j_holder(s.bindings) != "app.quench",
          "the app's own table is untouched by the edit — a commit reaches it through the store, not behind it");
    save_as("mine");
    check(preset_holder("mine").empty(),
          "…and the same name again leaves the saved preset as it was, rather than replacing it [" +
              preset_holder("mine") + "]");

    // ---- 9. RESET GOES BACK TO THE PRESET, not to the edit it is meant to throw away --------
    auto working_holder = [&]() {
      const RolltuiBindings* v = rolltui_bindings_store_working(s.store);
      return v ? alt_j_holder(v) : std::string();
    };
    check(working_holder() == "app.quench", "the working copy is the edited table, so a reset has something to undo");
    check(rolltui_bindings_store_modified(s.store) != 0, "…and it is labelled modified");
    while (back() == 1) {
    }
    s.send(ctrl_key('r'));
    check(working_holder().empty(),
          "Ctrl-R resets to the loaded preset: the working copy is what the file says again");
    check(rolltui_bindings_store_modified(s.store) == 0, "…so it is no longer modified");
    (void)escape;
  }

  // ---- 10. ESCAPE GOES BACK ONE LEVEL; ONLY FROM THE TOP DOES IT CLOSE THE POPUP ------------
  // A key being waited for goes first, then a load or a save being named, then the filter; at the top `back` says there is
  // nothing left, and the popup is the stack's to close.
  {
    const fs::path dir = scratch / "back";
    fs::create_directories(dir);
    Screen s;
    s.open(dir.string(), true);
    const RolltuiEvent enter = key_event(ROLLTUI_KEY_ENTER);
    auto back = [&]() { return rolltui_windows_back(s.windows, "anvil_edit", 10); };
    check(back() == 0, "at the top of the sheet there is no level to go back from");
    s.send(enter);  // the first row waits for a key
    check(back() == 1 && back() == 0, "a key being waited for is one level: back drops it, and the next back is at the top");
    s.type("/word");
    s.send(enter);
    check(back() == 1 && back() == 0, "a filter is one level");
    s.send(ctrl_key('s'));
    check(back() == 1 && back() == 0, "a save being named is one level");
    s.type("/word");
    s.send(enter);
    s.send(enter);
    int steps = 0;
    while (back() == 1 && steps < 10) ++steps;
    check(steps == 2, "a key waited for under a filter: two steps out, then the top [" + std::to_string(steps) + "]");
  }

  fs::remove_all(scratch);
  return report("rolltui_keys_kind_test");
}
