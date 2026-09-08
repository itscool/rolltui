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

RolltuiEvent key_event(unsigned char k, RolltuiCodepoint ch = 0, bool alt = false) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = k;
  e.key.ch = ch;
  e.key.alt = alt ? 1 : 0;
  return e;
}

// The one screen the in-process half drives: a `keys` window and nothing else, written here
// rather than read from a file because what is under test is the KIND, not the loader.
const char* kInProcessLayout = R"({
  "name": "anvil-inproc", "min_width": 0, "min_height": 0, "focus": "anvil_edit",
  "actions": {},
  "root": { "id": "anvil_edit", "content": "keys", "focusable": true }
})";

// The one string an assertion can compare: which action, if any, holds Alt-B in the `app` scope
// of a table.
std::string alt_b_holder(const RolltuiBindings* b) {
  RolltuiChord k{};
  k.key = ROLLTUI_KEY_CHAR;
  k.ch = 'b';
  k.alt = 1;
  std::size_t len = 0;
  const char* p = rolltui_bindings_action_for(b, &k, "app", 3, &len);
  return p ? std::string(p, len) : std::string();
}

// A screen that names `keys`, with the store handed over (or not), held open so a caller can
// send its own keystrokes and read the store back.
struct Screen {
  RolltuiContext* ctx = nullptr;
  RolltuiWindows* windows = nullptr;
  RolltuiWindowStack* stack = nullptr;
  RolltuiBindings* bindings = nullptr;
  RolltuiLayout* layout = nullptr;
  RolltuiPresetStore* store = nullptr;

  void open(const std::string& dir, bool hand_over_the_store) {
    ctx = rolltui_context_new();
    rolltui_context_set_library_defaults(ctx);
    rolltui_context_set_dir(ctx, dir.data(), dir.size());
    store = rolltui_preset_store_new(rolltui_preset_domain_bindings(ctx), dir.data(),
                                     dir.size(), 0, "", 0);
    RolltuiBindingsPresetReport start{};
    rolltui_preset_store_start(store, &start);
    rolltui_bindings_preset_report_release(&start);

    RolltuiLayoutReport lrep{};
    layout = rolltui_load_layout_text(kInProcessLayout, std::strlen(kInProcessLayout), nullptr, 0, nullptr, &lrep);
    rolltui_layout_report_release(&lrep);

    windows = rolltui_windows_new(ctx);
    stack = rolltui_window_stack_new();
    bindings = static_cast<RolltuiBindings*>(rolltui_preset_store_working(store));
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
    rolltui_preset_store_value_free(store, bindings);
    rolltui_preset_store_free(store);
    rolltui_context_free(ctx);
  }
};

struct Session {
  unsigned long long version_before = 0, version_after = 0;
  std::string holder_before, holder_after;
};

// One editing session over a screen that names `keys`: Alt-B onto `app.quench`, through the
// menu's own filter so the walk does not depend on where a row happens to sit. The action is
// the APP's declaration and nothing else's, so a session that can reach it is one editing the
// table the app is running on.
Session drive(const std::string& dir, bool hand_over_the_store) {
  Session out;
  Screen s;
  s.open(dir, hand_over_the_store);

  auto snapshot = [&](std::string& holder) {
    RolltuiBindings* v = static_cast<RolltuiBindings*>(rolltui_preset_store_working(s.store));
    if (v) {
      holder = alt_b_holder(v);
      rolltui_preset_store_value_free(s.store, v);
    }
  };
  out.version_before = rolltui_preset_store_version(s.store);
  snapshot(out.holder_before);

  const RolltuiEvent enter = key_event(ROLLTUI_KEY_ENTER);
  s.send(enter);        // Actions by scope
  s.type("app");
  s.send(enter);        // the app scope
  s.type("quench");
  s.send(enter);        // the action's level
  s.send(enter);        // add a chord (press it)…
  s.send(key_event(ROLLTUI_KEY_CHAR, 'b', /*alt=*/true));

  out.version_after = rolltui_preset_store_version(s.store);
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
  // `Type:` reads an underscore as a space, so a literal one is escaped.
  const std::string to_word_left = "Enter Type:input Enter Type:word\\_left Enter Enter";

  // ---- 1. A LAYOUT ALONE GIVES A WORKING EDITOR ---------------------------------------------
  int rc = 0;
  const std::string open = run(base + " --layout anvil --bindings anvil 2>&1", rc);
  check(rc == 0 && !open.empty(), "the studio runs a screen it has never heard of (rc " + std::to_string(rc) + ")");
  check(has(open, "keys editor") && has(open, "Actions by scope") && has(open, "Reset to the loaded preset"),
        "…and the window whose content is `keys` draws the editor's own menu");
  check(has(open, "Enter on an action: add, remove or clear its chords") &&
            has(open, "preset: (this app keeps no bindings presets)"),
        "…with the status line and the store line the kind draws, not the host");

  // ---- 2. IT IS DRIVABLE, with no host code between the keys and the model -------------------
  const std::string edited = run(base + " --layout anvil --bindings anvil --keys \"" + to_word_left + " AltB\" 2>&1", rc);
  check(has(edited, "remove Ctrl-Left") && has(edited, "remove Alt-B"),
        "keystrokes reach the editor: three levels down, the action's own chords are the items");
  check(has(edited, "bound Alt-B \xE2\x86\x92 word_left") && has(edited, "undo 1"),
        "…and the next key pressed IS the chord, committed, so there is something to undo");

  // ---- 3. THE LIVE-TABLE CONTROL ------------------------------------------------------------
  //
  // What makes an action editable is that something DECLARED it, and the only declaration of
  // `app.quench` in this process is the layout file. Its presence proves the table being edited
  // is the one the screen is running on rather than a preset parsed off disk.
  const std::string app_scope = run(base + " --layout anvil --bindings anvil --keys \"Enter Type:app Enter\" 2>&1", rc);
  check(has(app_scope, "quench  (unbound)"),
        "the tree carries an action declared by the LAYOUT and by no source, so the table is the live one");
  const std::string own_screen = run(base + " --bindings anvil --keys \"F7 Enter Type:app Enter\" 2>&1", rc);
  check(!has(own_screen, "quench"),
        "…and the studio's own screen, which declares no such action, has none: the layout supplied it");

  // ---- 4. THE ONE-WINDOW CONTROL ------------------------------------------------------------
  const std::string plain = run(base + " --layout anvil-plain --bindings anvil 2>&1", rc);
  check(rc == 0 && !plain.empty() && !has(plain, "keys editor") && !has(plain, "Actions by scope"),
        "the SAME screen with that one content string changed has no editor in it");

  // ---- 5. THE BINDINGS CONTROL --------------------------------------------------------------
  const std::string undone =
      run(base + " --layout anvil --bindings anvil --keys \"" + to_word_left + " AltB CtrlO\" 2>&1", rc);
  check(has(undone, "undone \xC2\xB7 undo 0"), "Ctrl-O undoes, because anvil's bindings file says editor.undo is Ctrl-O");
  const std::string shipped = run(base + " --layout anvil --keys \"" + to_word_left + " AltB CtrlO\" 2>&1", rc);
  check(!has(shipped, "undone") && has(shipped, "undo 1"),
        "…and with the shipped bindings the same key does nothing: the FILE supplied the chord");

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
          "both runs start from a table where Alt-B is free, so what lands is the edit");
    check(given.version_after != given.version_before && given.holder_after == "app.quench",
          "handed the app's bindings store, the commit is IN it: the version moved and Alt-B is quench's [" +
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
    const RolltuiEvent home = key_event(ROLLTUI_KEY_HOME);
    const RolltuiEvent left = key_event(ROLLTUI_KEY_LEFT);

    // BACK TO THE TOP LEVEL, and Left rather than Escape: Escape at the root CLOSES the menu,
    // and either only ascends ONE level — the fields below are three deep. A filter typed at the
    // wrong level matches nothing and the Enter after it does nothing, which is a save that
    // silently never happened.
    auto to_root = [&]() {
      for (int i = 0; i < 4; ++i) s.send(left);
      s.send(home);
    };
    auto save_as = [&](const char* name) {
      to_root();
      s.type("save");  // the filter reaches "Save keys as"
      s.send(enter);   // start editing the field
      s.type(name);
      s.send(enter);   // commit: the SaveAs outcome
    };
    auto preset_holder = [&](const char* name) {
      RolltuiBindingsPresetReport rep{};
      RolltuiBindings* v =
          static_cast<RolltuiBindings*>(rolltui_preset_store_get(s.store, name, std::strlen(name), &rep));
      std::string holder = "(no preset)";
      if (v) {
        holder = alt_b_holder(v);
        rolltui_preset_store_value_free(s.store, v);
      }
      rolltui_bindings_preset_report_release(&rep);
      return holder;
    };

    save_as("mine");
    check(preset_holder("mine").empty(),
          "Save keys as writes one, through the store the app handed over [" + preset_holder("mine") + "]");

    // Now rebind something and try the same name again.
    to_root();
    s.send(enter);
    s.type("app");
    s.send(enter);
    s.type("quench");
    s.send(enter);
    s.send(enter);
    s.send(key_event(ROLLTUI_KEY_CHAR, 'b', /*alt=*/true));
    check(alt_b_holder(s.bindings) != "app.quench",
          "the app's own table is untouched by the edit — a commit reaches it through the store, not behind it");
    save_as("mine");
    check(preset_holder("mine").empty(),
          "…and the same name again leaves the saved preset as it was, rather than replacing it [" +
              preset_holder("mine") + "]");

    // ---- 9. RESET GOES BACK TO THE PRESET, not to the edit it is meant to throw away --------
    auto working_holder = [&]() {
      RolltuiBindings* v = static_cast<RolltuiBindings*>(rolltui_preset_store_working(s.store));
      std::string holder;
      if (v) {
        holder = alt_b_holder(v);
        rolltui_preset_store_value_free(s.store, v);
      }
      return holder;
    };
    check(working_holder() == "app.quench", "the working copy is the edited table, so a reset has something to undo");
    to_root();
    s.type("loaded");  // "Reset to the loaded preset…"
    s.send(enter);
    check(working_holder().empty(),
          "Reset to the loaded preset re-reads the PRESET: the working copy is what the file says again");
  }

  fs::remove_all(scratch);
  return report("rolltui keys_kind_test");
}
