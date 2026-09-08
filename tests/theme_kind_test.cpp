//
// theme_kind_test.cpp — THE PROOF: an app gets a theme editor by NAMING it in a layout.
//
// `theme` is one of the library's own widget kinds, resolved through the same closed-table-
// then-host rungs as `menu` and `input`. So a screen that wants a theme editor says so in a
// file, and the binary that draws it needs no editor code — which is the whole difference
// between an editor one program owns and an editor the library does.
//
// Everything the screen is lives in rolltui/tests/fixtures/kiln/, copied into a scratch preset
// directory before the REAL rolltui-studio binary is run against it:
//
//   layouts/kiln.json         the design — a text: window and a `theme` window, and the two
//                             editor actions it declares
//   layouts/kiln-plain.json   the SAME screen with that one window changed to a text: one
//   bindings/kiln.json        the keys — the only thing that gives editor.undo a chord, and
//                             deliberately not the chord the studio's own tools suggest
//
// THE CONTROLS ARE WHAT CARRY THIS, in the shape files_only_test established:
//
//   THE SOURCE CONTROL. The word "kiln" — the layouts, the bindings file, both window ids —
//   appears in no source the binary is built from. Nothing special-cases this screen.
//
//   THE ONE-WINDOW CONTROL. kiln-plain is byte-for-byte the same screen with `theme` replaced
//   by `text:`. Every assertion about the editor flips, so it is the CONTENT STRING and not
//   the studio that puts an editor there.
//
//   THE BINDINGS CONTROL. Ctrl-O is `transcript.fold` in the shipped table and `editor.undo`
//   only in kiln's own file. The same keystrokes with the shipped bindings leave the edit
//   standing, so the undo in the run above came from the FILE.
//
//   THE STORE CONTROL (in process, below). A screen with a `theme` window edits nothing by
//   itself: the theme is the APP's, so a commit lands where the app said and nowhere else.
//   Handed a store, a commit is in it and its version moves; handed none, the same keystrokes
//   leave every store in the process untouched.
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

RolltuiEvent key_event(unsigned char k, RolltuiCodepoint ch = 0) {
  RolltuiEvent e{};
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = k;
  e.key.ch = ch;
  return e;
}

// The one screen the in-process half drives: a `theme` window and nothing else, written here
// rather than read from a file because what is under test is the KIND, not the loader.
const char* kInProcessLayout = R"({
  "name": "kiln-inproc", "min_width": 0, "min_height": 0, "focus": "kiln_edit",
  "actions": {},
  "root": { "id": "kiln_edit", "content": "theme", "focusable": true }
})";

// A theme's `text` colour, as the one number an assertion can compare.
unsigned int text_fg(const RolltuiJsonValue* colours) {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT];
  RolltuiStr name{};
  RolltuiThemeReport rep{};
  RolltuiEffectMap* eff =
      rolltui_theme_load(colours, ROLLTUI_MODE_DARK, rolltui_theme_default_vocab(), styles, &name, &rep);
  const RolltuiStyleColor c = styles[ROLLTUI_ROLE_TEXT].fg;
  rolltui_effect_map_free(eff);
  rolltui_str_free(&name);
  rolltui_theme_report_release(&rep);
  return (static_cast<unsigned int>(c.kind) << 24) | (static_cast<unsigned int>(c.r) << 16) |
         (static_cast<unsigned int>(c.g) << 8) | c.b;
}

// A screen that names `theme`, with the store handed over, held open so a caller can send its
// own keystrokes and read the store back. The two scenarios below want different keys and the
// same twelve lines of setup.
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
    store = rolltui_preset_store_new(rolltui_preset_domain_theme(ctx), dir.data(), dir.size(),
                                     0, "", 0);
    RolltuiThemePresetReport start{};
    rolltui_preset_store_start(store, &start);
    rolltui_theme_preset_report_release(&start);

    RolltuiLayoutReport lrep{};
    layout = rolltui_load_layout_text(kInProcessLayout, std::strlen(kInProcessLayout), nullptr, 0, nullptr, &lrep);
    rolltui_layout_report_release(&lrep);

    windows = rolltui_windows_new(ctx);
    stack = rolltui_window_stack_new();
    bindings = rolltui_bindings_clone(rolltui_bindings_default(ctx));
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));

    RolltuiWidgetEnv env{};
    rolltui_context_set_env(ctx, &env);
    rolltui_context_set_bindings(ctx, bindings);
    rolltui_windows_sync(windows, stack);
    rolltui_windows_layout(windows, stack, RolltuiRect{0, 0, 80, 30});

    // THE ONE LINE AN APP WRITES. Everything after it is the editor driving itself.
    if (hand_over_the_store) rolltui_windows_set_theme_store(windows, "theme", 5, store, /*persist=*/0);
  }

  void send(const RolltuiEvent& e) { rolltui_windows_handle(windows, "kiln_edit", 9, &e); }
  void type(const char* text) {
    for (const char* p = text; *p; ++p) send(key_event(ROLLTUI_KEY_CHAR, static_cast<RolltuiCodepoint>(*p)));
  }

  ~Screen() {
    rolltui_bindings_free(bindings);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_layout_free(layout);
    rolltui_preset_store_free(store);
    rolltui_context_free(ctx);
  }
};

// One editing session over a screen that names `theme`: build the windows, hand over the store
// (or not), send the keystrokes, and report the store's version and its `text` colour after.
struct Session {
  unsigned long long version_before = 0, version_after = 0;
  unsigned int text_fg_before = 0, text_fg_after = 0;
};

Session drive(const std::string& dir, bool hand_over_the_store) {
  Session out;
  Screen s;
  s.open(dir, hand_over_the_store);

  auto snapshot = [&](unsigned int& fg) {
    RolltuiThemePresetValue* v = static_cast<RolltuiThemePresetValue*>(rolltui_preset_store_working(s.store));
    if (v) {
      fg = text_fg(v->colours);
      rolltui_preset_store_value_free(s.store, v);
    }
  };
  out.version_before = rolltui_preset_store_version(s.store);
  snapshot(out.text_fg_before);

  // Roles › the first role › fg › the next colour, committed.
  const RolltuiEvent enter = key_event(ROLLTUI_KEY_ENTER);
  const RolltuiEvent down = key_event(ROLLTUI_KEY_DOWN);
  s.send(enter);
  s.send(enter);
  s.send(enter);
  s.send(down);
  s.send(down);
  s.send(enter);

  out.version_after = rolltui_preset_store_version(s.store);
  snapshot(out.text_fg_after);
  return out;
}

}  // namespace

int main() {
  const std::string studio = ROLLTUI_STUDIO_BIN;
  const fs::path scratch = fs::temp_directory_path() / ("rolltui-kiln-" + std::to_string(getpid()));
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  fs::copy(fs::path(ROLLTUI_FIXTURE_DIR) / "kiln", scratch, fs::copy_options::recursive);
  const std::string presets = " --presets '" + scratch.string() + "'";
  const std::string base = studio + " --frame 100x30" + presets + " --theme default-dark";

  // ---- 1. A LAYOUT ALONE GIVES A WORKING EDITOR ---------------------------------------------
  int rc = 0;
  const std::string open = run(base + " --layout kiln --bindings kiln 2>&1", rc);
  check(rc == 0 && !open.empty(), "the studio runs a screen it has never heard of (rc " + std::to_string(rc) + ")");
  check(has(open, "theme editor") && has(open, "Roles") && has(open, "Generate a theme (seeded)"),
        "…and the window whose content is `theme` draws the editor's own menu");
  check(has(open, "badges:") && has(open, "Enter commits, Esc cancels"),
        "…with the classification and the recovery model it states, drawn by the kind and not by the host");

  // ---- 2. IT IS DRIVABLE, with no host code between the keys and the model -------------------
  const std::string edited =
      run(base + " --layout kiln --bindings kiln --keys \"Enter Type:heading Enter Enter Down Down Enter\" 2>&1", rc);
  check(has(edited, "md_heading  fg "), "keystrokes reach the editor: three levels down, the role sample names it");
  check(has(edited, "undo 1"), "…and Enter on a colour COMMITS, so there is something to undo");

  // ---- 3. THE ONE-WINDOW CONTROL ------------------------------------------------------------
  const std::string plain = run(base + " --layout kiln-plain --bindings kiln 2>&1", rc);
  check(rc == 0 && !plain.empty() && !has(plain, "badges:") && !has(plain, "Generate a theme (seeded)"),
        "the SAME screen with that one content string changed has no editor in it");

  // ---- 4. THE BINDINGS CONTROL --------------------------------------------------------------
  const std::string undone =
      run(base + " --layout kiln --bindings kiln --keys \"Enter Type:heading Enter Enter Down Down Enter CtrlO\" 2>&1",
          rc);
  check(has(undone, "undone \xC2\xB7 undo 0"), "Ctrl-O undoes, because kiln's bindings file says editor.undo is Ctrl-O");
  const std::string shipped =
      run(base + " --layout kiln --keys \"Enter Type:heading Enter Enter Down Down Enter CtrlO\" 2>&1", rc);
  check(!has(shipped, "undone") && has(shipped, "undo 1"),
        "…and with the shipped bindings the same key does nothing: the FILE supplied the chord");

  // ---- 5. THE SOURCE CONTROL ----------------------------------------------------------------
  {
    std::vector<std::string> namers;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
      const std::string path = e.path().string();
      const std::string ext = e.path().extension().string();
      if (ext != ".h" && ext != ".hpp" && ext != ".c" && ext != ".cpp") continue;
      if (path.find("/tests/") != std::string::npos || path.find("/third_party/") != std::string::npos) continue;
      if (has(read_file(path), "kiln")) namers.push_back(path);
    }
    check(namers.empty(), "no source the studio is built from names this screen" +
                              (namers.empty() ? "" : " — " + namers.front()));
    check(has(read_file(std::string(ROLLTUI_FIXTURE_DIR) + "/kiln/layouts/kiln.json"), "kiln"),
          "…and the matcher does find the word where it belongs, so the absence above is real");
  }

  // ---- 6. WHERE THE EDIT LANDS --------------------------------------------------------------
  {
    const fs::path a = scratch / "store-given";
    const fs::path b = scratch / "store-withheld";
    fs::create_directories(a);
    fs::create_directories(b);
    const Session given = drive(a.string(), true);
    const Session withheld = drive(b.string(), false);
    check(given.version_after != given.version_before && given.text_fg_after != given.text_fg_before,
          "handed the app's theme store, the commit is IN it: the version moved and so did the colour");
    check(withheld.version_after == withheld.version_before && withheld.text_fg_after == withheld.text_fg_before,
          "…and withheld, the same keystrokes leave it untouched: the theme is the APP's");
    check(given.text_fg_after != withheld.text_fg_after,
          "…and the two runs started from the same theme, so what landed is the edit");
  }

  // ---- 7. A SAVE NEVER SILENTLY REPLACES SOMEONE ELSE'S PRESET ------------------------------
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
    const RolltuiEvent down = key_event(ROLLTUI_KEY_DOWN);
    const RolltuiEvent home = key_event(ROLLTUI_KEY_HOME);
    const RolltuiEvent left = key_event(ROLLTUI_KEY_LEFT);

    // BACK TO THE TOP LEVEL, and Left rather than Escape: Escape at the root CLOSES the menu,
    // and one of either only ascends ONE level — the fields below are three deep. A filter typed
    // at the wrong level matches nothing and the Enter after it does nothing, which is a save
    // that silently never happened.
    auto to_root = [&]() {
      for (int i = 0; i < 4; ++i) s.send(left);
      s.send(home);
    };
    auto save_as = [&](const char* name) {
      to_root();
      s.type("save");  // the filter reaches "Save as preset"
      s.send(enter);   // start editing the field
      s.type(name);
      s.send(enter);   // commit: the SaveAs outcome
    };
    auto preset_text_fg = [&](const char* name) {
      RolltuiThemePresetReport rep{};
      RolltuiThemePresetValue* v =
          static_cast<RolltuiThemePresetValue*>(rolltui_preset_store_get(s.store, name, std::strlen(name), &rep));
      unsigned int fg = 0;
      if (v) {
        fg = text_fg(v->colours);
        rolltui_preset_store_value_free(s.store, v);
      }
      rolltui_theme_preset_report_release(&rep);
      return fg;
    };

    save_as("mine");
    const unsigned int first = preset_text_fg("mine");
    check(first != 0, "Save as preset writes one, through the store the app handed over");

    // Now change the theme and try the same name again.
    to_root();
    s.send(enter);  // Roles
    s.send(enter);  // the first role
    s.send(enter);  // fg
    s.send(down);
    s.send(down);
    s.send(enter);  // commit
    save_as("mine");
    check(preset_text_fg("mine") == first,
          "…and the same name again leaves the saved preset as it was, rather than replacing it");

    // ---- 8. RESET GOES BACK TO THE PRESET, not to the edit it is meant to throw away --------
    auto working_text_fg = [&]() {
      RolltuiThemePresetValue* v = static_cast<RolltuiThemePresetValue*>(rolltui_preset_store_working(s.store));
      unsigned int fg = 0;
      if (v) {
        fg = text_fg(v->colours);
        rolltui_preset_store_value_free(s.store, v);
      }
      return fg;
    };
    const unsigned int edited = working_text_fg();
    check(edited != first, "the working copy is the edited theme, so a reset has something to undo");
    to_root();
    s.type("loaded");  // "Reset to the loaded preset…"
    s.send(enter);
    check(working_text_fg() == first,
          "Reset to the loaded preset re-reads the PRESET: the working copy is what the file says again");
  }

  fs::remove_all(scratch);
  return report("rolltui theme_kind_test");
}
