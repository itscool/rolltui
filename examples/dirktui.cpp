//
// rolltui/examples/dirktui.cpp — `dirktui`, a directory picker for the shell; `dirk` is its shell function.
//
// It is a READ-ONLY file-system browser in the shape macOS calls column view (Miller columns):
// side-by-side lists, the selection in a column filling the one to its right, a horizontal
// scroll when the path is deeper than the window, and a vertical scroll per column. It never
// writes, renames, moves or deletes anything. Reading a directory is the LIBRARY's
// (`rolltui_dir_read`), so this file's whole remaining contact with the file system is one `stat`
// asking whether a typed path is a directory before jumping to it.
//
// ============================================================================================
// THE CONTRACT WITH THE SHELL, which is the whole reason it is a utility and not a demo
//
//   * It DRAWS on /dev/tty and ANSWERS on stdout, always. A shell function captures stdout with
//     `$(dirktui)`, so no frame may ever reach it; the terminal is opened by name rather than
//     inherited. fzf does the same, for the same reason.
//   * Enter on a FOLDER leaves with its path on stdout, one line, exit 0. Enter on a FILE does
//     what the `file_enter` setting says: open it and stay (the default), open it and leave
//     with nothing printed, leave with its path (exit 0: the shell lands in its folder), or
//     leave with its path RELATIVE to where dirk started and exit 3 — THE STATUS IS THE VERB:
//     0 is "go there", 3 is "put this on the command line". The shell never opens anything.
//   * Escape (or Ctrl-Q / Ctrl-C anywhere) CANCELS: nothing is printed and the exit status is 1.
//     "Nothing printed" and "exit 0" never coincide, because `cd ""` is `cd ~`, silently.
//   * `dirktui init zsh|bash|fish` prints the shell integration: a `dirk` function that browses
//     then goes where you chose, and a Right Arrow binding that opens the browser only when the
//     cursor is already at the end of the line. Usage/no-terminal is exit 2.
//
// ============================================================================================
// WHAT IS THE APP'S AND WHAT IS THE SCREEN'S
//
// The screen is FILES (`examples/presets/`): the layout names the windows and DECLARES every
// action, the bindings file says which chord runs each one, and the menu is a file too. No
// string below names a window id, and `dirktui_test`'s grep asserts it — the same control
// `files_only_test` runs for the studio.
//
// The BROWSER OWNS ITS MODEL, deliberately. It is the library's aligned probe: a widget with
// data and structure of its own — children, two scroll axes, a selection that propagates
// sideways, a width that depends on its contents — which is the case a two-callback adapter
// cannot serve. Every place the public header could not do something is a wall to record.
//
#include <dirent.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "rolltui/rolltui.h"

extern char** environ;  // for posix_spawnp: not declared by any header on Darwin

// ADDITIVE, NOT SUBTRACTIVE. `dirk` is the product and cannot drive itself: the
// script vocabulary is not compiled into it. `dirk-selftest` is the same source plus
// this, which is what a golden frame is rendered by. The app code either binary runs is the
// same code, so the product binary is the one the app was verified through.
#ifdef ROLLTUI_SELFTEST
#include "rolltui/selftest/script.hpp"
#endif

namespace {

constexpr const char* kPicker = "filepicker";  // the library's column browser, the base window's content

// ---- what the host and the widget agree on ------------------------------------------------
// One struct, owned by the app, BORROWED by the widget through its factory ctx. This is how a
// host kind reaches the host's own state — the shape paint uses for its brush, and the reason
// `rolltui_windows_bindings` being INTERNAL costs nothing: a host already holds its own table
// and hands it over here (wall 1 in the phase file).
enum class Sort { Name, Size, Modified };

// ---- WHAT OPENS WHAT: known software, detected, chosen per type ----------------------------
// A document is opened by a PROGRAM chosen for its TYPE GROUP. The programs are a table of known
// software; which of them exist on this machine is detected once at start (a command on PATH, or
// an application bundle in the applications directories), and the settings menu offers, per
// group, exactly what was found — plus the system opener, always, and "the command line", which
// is not opening at all. A person's choice is kept per group; a choice that is no longer
// installed falls back to the group's default rather than to nothing. No environment variable
// is read for any of this: $EDITOR names one program for every type, and that is the question
// this table exists to answer per type.
struct Program {
  const char* id;
  const char* label;
  const char* exe;   // a command on PATH, or (`mac_app`) an application bundle's name
  const char* arg;   // one fixed argument before the file, or NULL
  bool terminal;     // takes the terminal over: dirktui steps aside and comes back when it exits
  bool mac_app;      // runs through `open -a`
};
static const Program kPrograms[] = {
    {"nvim", "Neovim", "nvim", nullptr, true, false},
    {"vim", "Vim", "vim", nullptr, true, false},
    {"hx", "Helix", "hx", nullptr, true, false},
    {"micro", "micro", "micro", nullptr, true, false},
    {"nano", "nano", "nano", nullptr, true, false},
    {"emacs", "Emacs (in the terminal)", "emacs", "-nw", true, false},
    {"less", "less", "less", nullptr, true, false},
    {"code", "Visual Studio Code", "code", nullptr, false, false},
    {"zed", "Zed", "zed", nullptr, false, false},
    {"subl", "Sublime Text", "subl", nullptr, false, false},
    {"textedit", "TextEdit", "TextEdit", nullptr, false, true},
    {"preview", "Preview", "Preview", nullptr, false, true},
};
static constexpr const char* kSystemProgram = "system";  // the platform's opener: `open`, `xdg-open`
static constexpr const char* kShellProgram = "shell";    // not opened: handed to the command line
struct TypeGroup {
  const char* id;
  const char* label;
  const char* exts;    // space-separated, lower-case
  const char* prefer;  // program ids in the order the group's DEFAULT is picked from what is installed
};
static const TypeGroup kGroups[] = {
    {"text", "Text (txt, md, json, log…)",
     "txt md markdown rst log csv tsv json yaml yml toml ini cfg conf xml rtf tex",
     "nvim hx micro code zed subl vim nano emacs textedit less system"},
    {"code", "Code (py, js, c, sh…)",
     "py rb js ts tsx jsx mjs c cc cpp cxx h hpp hh m mm swift go rs java kt scala lua sql php pl sh bash zsh fish cmake mk",
     "code zed subl nvim hx micro vim nano emacs textedit system"},
    {"web", "Web (html, svg, css)", "html htm svg css", "system code zed subl nvim hx micro vim"},
    {"docs", "Pictures, PDFs, media and office files",
     "pdf png jpg jpeg gif webp bmp tiff heic mp3 wav m4a mp4 mov m4v doc docx xls xlsx ppt pptx pages numbers key epub",
     "system preview"},
};
static const Program* program_named(const std::string& id) {
  for (const Program& p : kPrograms) if (id == p.id) return &p;
  return nullptr;
}
static bool word_in(const char* list, const std::string& word) {
  std::istringstream in(list);
  std::string w;
  while (in >> w) if (w == word) return true;
  return false;
}
static const TypeGroup* group_of(const std::string& name) {
  const std::size_t dot = name.rfind('.');
  if (dot == std::string::npos || dot + 1 >= name.size()) return nullptr;
  std::string ext = name.substr(dot + 1);
  for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  for (const TypeGroup& g : kGroups) if (word_in(g.exts, ext)) return &g;
  return nullptr;
}
// Which known programs are installed: a command on PATH, or a bundle under one of `apps_dirs`
// (colon-separated). Read once; the answer is the menu's contents.
static std::set<std::string> detect_programs(const std::string& apps_dirs) {
  std::set<std::string> out;
  std::vector<std::string> path, apps;
  auto split = [](const char* s, std::vector<std::string>& into) {
    std::string cur;
    for (const char* p = s ? s : ""; ; ++p) {
      if (*p == ':' || *p == '\0') { if (!cur.empty()) into.push_back(cur); cur.clear(); if (!*p) break; }
      else cur += *p;
    }
  };
  split(std::getenv("PATH"), path);
  split(apps_dirs.c_str(), apps);
  for (const Program& p : kPrograms) {
    bool found = false;
    if (p.mac_app) {
      for (const std::string& d : apps) {
        struct stat st{};
        if (stat((d + "/" + p.exe + ".app").c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { found = true; break; }
      }
    } else {
      for (const std::string& d : path)
        if (access((d + "/" + p.exe).c_str(), X_OK) == 0) { found = true; break; }
    }
    if (found) out.insert(p.id);
  }
  return out;
}

struct Options {
  bool hidden = true;  // dotfiles shown unless a person turns them off
  Sort sort = Sort::Name;
  bool motion = true;    // the effects and the column slide; off is a still app
  bool dividers = true;  // a hairline in the margin between columns, in the border colour
  // WHAT ENTER ON A FILE DOES. A folder is always entered. A file is a leaf: an EXECUTABLE goes
  // to the command line typed out, so arguments can follow (a picker never runs anything); a
  // document opens with the program chosen for its TYPE (`open_with`, from what is installed, see
  // `kGroups`); a type nobody listed goes to the command line too. `leave` overrides all of it:
  // every file goes to the command line, and the cursor leaves with it.
  bool leave = false;
  bool land_in_file_folder = false;  // the command line lands in the file's folder with `./name` (exit 4), else where dirk started (exit 3)
  bool copy_relative = false;        // paths handed out (copy, the command line): relative to where dirk started, or absolute
  std::map<std::string, std::string> open_with;  // type group id -> program id; absent means the group's own default
};

// A `RolltuiStr` as a `std::string`, at the sites that want one. The library's own vocabulary
// never names a std:: type, so a host that composes with std::string converts here — one line,
// judged per call site, which is the boundary rule rather than a wrapper around the API.
std::string str_of(const RolltuiStr& s) { return std::string(s.p ? s.p : "", s.n); }

// NO `Entry` OF ITS OWN. `RolltuiDirEntry` carries exactly what this app kept — the name, whether
// it is a directory, whether it could be described, its size, its time and its mode — so a
// parallel struct would be a second thing to drift and a copy per directory to keep it in step.

// ---- the two effect KINDS this app brings, rung 2 of the effects table -------------------------
// Each is a pure function of (elapsed, index, length, base style) and answers for ONE cell — the
// contract every kind is held to. Each picks a colour the THEME named: the role's foreground on
// the cell's own background, so a lit cell on a highlighted row keeps the row's highlight. A
// kind never invents a colour and never changes a glyph's width. What LOOKS random is a hash of
// (step, cell), so the same tick always draws the same picture — which is what makes a moving
// frame a golden frame.
unsigned hash32(unsigned x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
// A SPARK'S LIFE: it POPS — a a brighter colour in the role's colour, bold — and then FADES back to the cell's
// own colour over the rest of its life. `k` is how much of the role is left, 1 at the pop and 0
// when it is over. In 24-bit colour the fade is a real blend; on an indexed or mono screen the
// spark is the role while `k` is above a half and the cell's own colour after, since a blend is
// a colour the theme did not name.
RolltuiStyleColor blend_to(RolltuiStyleColor from, RolltuiStyleColor to, double k) {
  if (from.kind != RolltuiStyleColor::Kind::Rgb || to.kind != RolltuiStyleColor::Kind::Rgb) return k > 0.5 ? to : from;
  auto mix = [&](unsigned char a, unsigned char b) { return static_cast<unsigned char>(a + (b - a) * k + 0.5); };
  from.r = mix(from.r, to.r);
  from.g = mix(from.g, to.g);
  from.b = mix(from.b, to.b);
  return from;
}
// A BRIGHTER VERSION OF A COLOUR: pulled toward white by `amount`. What a shimmer or a spark
// goes UP to — never toward the background, which is where it would disappear. Without RGB
// the colour is returned as it is and boldness carries the brightening.
RolltuiStyleColor brighter(RolltuiStyleColor c, double amount) {
  if (c.kind != RolltuiStyleColor::Kind::Rgb) return c;
  RolltuiStyleColor white{};
  white.kind = RolltuiStyleColor::Kind::Rgb;
  white.r = white.g = white.b = 255;
  return blend_to(c, white, amount);
}

// A SPARK, at `k` of the way up: the floor is the role's own colour, the top is a near-white
// version of it. COLOUR ONLY: a glyph flickered onto the letter was tried twice and taken out
// twice — the letter is what is being decorated, and it stays the letter.
void spark(const RolltuiEffectSpec* s, const RolltuiStyle* styles, const RolltuiEffectCell* in, std::size_t role, double k,
           RolltuiEffectOut* out) {
  // The top of a spark is WHITE, not most of the way there: the climb from the role's colour to
  // white runs over the upper 60% of `k`, so a fresh spark pops white and the fade back passes
  // through the colour before it settles at the floor.
  const double toward_white = k > 0.4 ? (k - 0.4) / 0.6 : 0.0;
  const RolltuiStyleColor lit = brighter(styles[s->roles[role % s->role_count]].fg, toward_white > 1.0 ? 1.0 : toward_white);
  out->has_style = 1;
  out->style = in->base;
  out->style.fg = blend_to(in->base.fg, lit, k);
  out->style.bold = k > 0.5 ? 1 : in->base.bold;
}

// `dirk_sparkle`: the name is TINTED with the spark colour all the time — a floor of a third,
// so a trail row reads as the path — and sparks at a rate PER WORD, not per cell: two sparks a
// second on every word whatever its length, so each cell's period is the word's length times
// half a second, with a phase and a little jitter hashed from the cell so words do not tick in
// step. A spark pops to white and fades back to the floor over a second and a half.
void fx_sparkle(void*, const RolltuiEffectSpec* s, const RolltuiStyle* styles, const void*, const RolltuiEffectCell* in,
                RolltuiEffectOut* out) {
  static constexpr double kFloor = 0.35;          // how much of the spark colour the name keeps between sparks
  static constexpr double kSparksPerSecond = 2.0; // per WORD
  const unsigned seed = hash32(static_cast<unsigned>(in->index) * 2654435761u + static_cast<unsigned>(in->length) * 40503u + 7u);
  const unsigned long long len = in->length > 0 ? static_cast<unsigned long long>(in->length) : 1;
  const unsigned long long base = static_cast<unsigned long long>(1000.0 / kSparksPerSecond);
  const unsigned long long period = len * (base * 8 / 10 + seed % (base * 4 / 10 + 1));   // ±20% jitter
  const unsigned long long phase = (seed >> 8) % period;
  const unsigned long long life = 1500;
  const unsigned long long t = (in->elapsed_ms + phase) % period;
  double k = kFloor;
  if (t < life) {
    const double u = static_cast<double>(t) / life;       // 0 at the pop, 1 when it is over
    k = kFloor + (1.0 - kFloor) * (1.0 - u) * (1.0 - u); // down to the floor, fast at first
  }
  spark(s, styles, in, 0, k, out);
}

// `dirk_glow`: the cursor's word, TINTED in the role from end to end, with a soft band that
// bounces left and right across it — the shimmer a text UI puts on a word that is live, rather
// than a block of background behind it. The band is a BRIGHTER version of the same colour,
// pulled toward white, never a second role: a band that went toward the background would make
// the word vanish where it passed. Its centre follows a triangle over the period, so it turns at
// the ends instead of wrapping; its brightness falls off as a bell, `width` cells wide.
void fx_glow(void*, const RolltuiEffectSpec* s, const RolltuiStyle* styles, const void*, const RolltuiEffectCell* in,
             RolltuiEffectOut* out) {
  const int period = s->period_ms > 0 ? s->period_ms : 2000;
  const double width = s->width > 0 ? s->width : 3.0;
  const double u = static_cast<double>(in->elapsed_ms % static_cast<unsigned long long>(period)) / period;
  const double tri = u < 0.5 ? u * 2.0 : (1.0 - u) * 2.0;
  const double centre = tri * (in->length > 1 ? in->length - 1 : 0);
  const double d = (in->index - centre) / width;
  const double peak = std::exp(-d * d * 2.0);
  const RolltuiStyleColor tint = styles[s->roles[0]].fg;
  out->has_style = 1;
  out->style = in->base;
  out->style.fg = brighter(tint, peak * 0.75);
  out->style.bold = peak > 0.6 ? 1 : in->base.bold;
}

// A HUE AS A COLOUR, for the one effect that is allowed to invent one. The library's rule that
// an effect never invents a colour keeps a mono screen legible and a theme in charge; it holds
// here everywhere it can be checked — on a screen without RGB this kind falls back to the theme's
// roles — and is set aside, deliberately, for the celebration below in 24-bit colour, where a
// rainbow has no role to be named by. `h` in [0,1), saturated, full value.
RolltuiStyleColor hue(double h) {
  h -= std::floor(h);
  const double x = h * 6.0;
  const int i = static_cast<int>(x);
  const double f = x - i, s = 0.75;
  const double p = 1.0 - s, q = 1.0 - s * f, t = 1.0 - s * (1.0 - f);
  double r = 1, g = 1, b = 1;
  switch (i % 6) {
    case 0: r = 1; g = t; b = p; break;
    case 1: r = q; g = 1; b = p; break;
    case 2: r = p; g = 1; b = t; break;
    case 3: r = p; g = q; b = 1; break;
    case 4: r = t; g = p; b = 1; break;
    default: r = 1; g = p; b = q; break;
  }
  RolltuiStyleColor c{};
  c.kind = RolltuiStyleColor::Kind::Rgb;
  c.r = static_cast<unsigned char>(r * 255 + 0.5);
  c.g = static_cast<unsigned char>(g * 255 + 0.5);
  c.b = static_cast<unsigned char>(b * 255 + 0.5);
  return c;
}

// `dirk_burst`: ONE shot on the name just opened — a RAINBOW slides across the word, rising
// fast, holding, and fading back to the name's own colour by the end of the period, Colour only, like every
// effect here. Past the period, nothing. Without RGB the word cycles the theme's
// roles instead, so the moment still happens on every screen.
void fx_burst(void*, const RolltuiEffectSpec* s, const RolltuiStyle* styles, const void*, const RolltuiEffectCell* in,
              RolltuiEffectOut* out) {
  const int period = s->period_ms > 0 ? s->period_ms : 1200;
  if (in->elapsed_ms >= static_cast<unsigned long long>(period)) return;
  const double u = static_cast<double>(in->elapsed_ms) / period;
  const double k = u < 0.15 ? u / 0.15 : u < 0.45 ? 1.0 : (1.0 - u) / 0.55;   // rise, hold, fade
  const double along = in->length > 1 ? static_cast<double>(in->index) / (in->length - 1) : 0.0;
  const double h = along * 0.9 - u * 1.6;                                      // the gradient slides left
  out->has_style = 1;
  out->style = in->base;
  if (in->base.fg.kind == RolltuiStyleColor::Kind::Rgb) {
    out->style.fg = blend_to(in->base.fg, hue(h), k * k);
  } else {
    const std::size_t which = (static_cast<std::size_t>(in->index) + static_cast<std::size_t>(u * 12)) % s->role_count;
    if (k > 0.5) out->style.fg = styles[s->roles[which]].fg;
  }
  out->style.bold = k > 0.4 ? 1 : in->base.bold;
}

}  // namespace

namespace {

std::string user_presets_dir();  // defined below, with the file loaders

// ---- the app ---------------------------------------------------------------------------
// APP LIFETIME, RELEASED IN ONE DESTRUCTOR — paint's shape, and for its reason: none of these
// is per-frame, so no wrapper type earns its place. A missed release leaks once and
// `rolltui_shutdown`'s `live_bytes == 0` is what catches it.
struct App {
  RolltuiContext* ctx = rolltui_context_new();  // OWNED: this app's session (Phase 25)
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  RolltuiEffectScratch* effect_scratch = rolltui_effect_scratch_new();  // OWNED: the applier's working memory
  RolltuiDrawScratch* draw_scratch = rolltui_draw_scratch_new();
  RolltuiBindings* bindings = rolltui_bindings_clone(rolltui_bindings_default(ctx));
  RolltuiWindows* windows = rolltui_windows_new(ctx);
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  RolltuiComposeScratch* compose_scratch = rolltui_compose_scratch_new();
  RolltuiLayout* layout = nullptr;  // OWNED (Phase 23: a layout is a handle)
  // OWNED: the user's own theme and key presets. An app that cannot change how it looks is an
  // app the library's editors have nothing to edit — the kinds are the library's, the STORE is
  // what makes them this app's.
  RolltuiPresetStore* theme_store = nullptr;
  RolltuiPresetStore* keys_store = nullptr;
  unsigned long long theme_seen = 0;
  Options opt;
  std::string root;
  std::string note;   // the library's own report for this frame
  std::string hint;   // this app's own last word (a bad path, a jump)
  // CALLER-FILLED, one per run: the status line's fields, reset and refilled every frame so
  // the array and each row's buffer are reused rather than rebuilt.
  RolltuiRows status_rows{};
  std::string keys_hint;  // "F1 help · F2 settings · c copy", from the live bindings, built once
  void build_keys_hint() {
    struct Row { const char* action; const char* what; };
    static const Row rows[] = {{"app.help", "help"}, {"app.menu", "settings"}, {"picker.copy", "copy"}, {"app.jump", "jump"}};
    keys_hint.clear();
    for (const Row& r : rows) {
      const std::size_t n = rolltui_bindings_chord_count(bindings, r.action, std::strlen(r.action));
      if (n == 0) continue;
      RolltuiChord c{};
      rolltui_bindings_chord_at(bindings, r.action, std::strlen(r.action), 0, &c);
      char buf[ROLLTUI_CHORD_STRING_MAX];
      const std::size_t bn = rolltui_chord_display(&c, buf, sizeof buf);
      if (!keys_hint.empty()) keys_hint += "  ";
      keys_hint.append(buf, bn);
      keys_hint += ' ';
      keys_hint += r.what;
    }
  }
  int w = 100, h = 30;
  bool quit = false;
  unsigned long long now_ms = 0;  // the frame clock; 0 in a headless frame, where nothing moves
  RolltuiEffectReport last_fx{};  // what the last frame's effects touched: a self-test reads it
  std::size_t last_marks = 0;
  // How soon this frame wants redrawing: a sliding column asks for the next tick, a marked span
  // whose effect moves asks for its own interval, else `idle`.
  int poll_timeout_ms(const RolltuiFrame* f, int idle) {
    RolltuiPickerStatus st{};
    const bool moving = rolltui_windows_picker_status(windows, kPicker, 10, &st) && st.moving;
    rolltui_picker_status_release(&st);
    int want = moving ? 16 : idle;
    if (opt.motion && f && rolltui_frame_mark_count(f) != 0 && effects && !rolltui_effect_map_empty(effects)) {
      const int tick = rolltui_effects_tick_ms(ctx, f, effects);
      if (tick > 0 && tick < want) want = tick;
    }
    return want;
  }
  // THE EXIT CONTRACT: `chosen` is printed and the status is 0 ONLY when an accept happened.
  // Every other way out — cancel, a global quit — prints nothing and exits 1. The shell function
  // reads exactly this pair, and an empty path with status 0 would send it to `cd ""`.
  std::string chosen;
  int exit_code = 1;

  // THIS APP'S OWN MOTION VOCABULARY, on the session: three states its widget marks with, three
  // kinds a theme may name. Registered before any theme loads, because the vocabulary a theme
  // file is read against (`rolltui_theme_vocab`) is built from what has been registered.
  std::string effects_json;  // the app's mapping file, state -> kind + role; merged onto every theme
  std::string menu_json;     // the app's settings menu, a file like the rest of its screen
  bool menu_dirty = false;   // the settings popup was just opened: its boxes need the live values
  App() {
    layout = rolltui_layout_new();
    rolltui_context_set_library_defaults(ctx);
    rolltui_effect_register(ctx, "dirk_sparkle", 12, fx_sparkle, nullptr, nullptr);
    rolltui_effect_register(ctx, "dirk_glow", 9, fx_glow, nullptr, nullptr);
    rolltui_effect_register(ctx, "dirk_burst", 10, fx_burst, nullptr, nullptr);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() {
    rolltui_preset_store_free(keys_store);
    rolltui_preset_store_free(theme_store);
    rolltui_rows_release(&status_rows);
    rolltui_layout_free(layout);
    rolltui_compose_scratch_free(compose_scratch);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_bindings_free(bindings);
    rolltui_draw_scratch_free(draw_scratch);
    rolltui_effect_scratch_free(effect_scratch);
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
    merge_effects();
  }

  // THE APP'S MAPPING ONTO A PERSON'S THEME. The theme names the colours; this app's file says
  // which role and which kind each of its own states wears; the merge widens the map to the
  // session's vocabulary and adds the rows. A theme that already maps a `dirk.*` state keeps
  // its row too (the specs STACK), so a person's theme can restyle this app without editing
  // the app's file. What the file gets wrong is said, once, where a developer is looking.
  void merge_effects() {
    if (!effects || effects_json.empty()) return;
    RolltuiThemeReport rep{};
    rolltui_theme_effects_merge(effects, effects_json.data(), effects_json.size(), rolltui_theme_vocab(ctx), &rep);
    if (rep.error.n || rep.unknown_keys_n || rep.bad_values_n) {
      std::fprintf(stderr, "dirktui: effects file: %s", rep.error.n ? rep.error.c_str() : "");
      for (std::size_t i = 0; i < rep.unknown_keys_n; ++i) std::fprintf(stderr, " unknown %s", rep.unknown_keys[i].c_str());
      for (std::size_t i = 0; i < rep.bad_values_n; ++i) std::fprintf(stderr, " bad %s", rep.bad_values[i].c_str());
      std::fputc('\n', stderr);
    }
    rolltui_theme_report_release(&rep);
  }

  // The look comes from the STORE once there is one, so an edit made in the theme editor is what
  // the next frame draws. Falls back to the built-in when the store has nothing loadable, which
  // is what keeps a broken preset directory from being a blank screen.
  void sync_theme() {
    if (!theme_store) return;
    RolltuiThemePresetValue* w = (RolltuiThemePresetValue*)rolltui_preset_store_working(theme_store);
    if (!w) return;
    RolltuiThemeReport rep{};
    RolltuiStyle got[ROLLTUI_ROLE_COUNT]{};
    RolltuiStr name{};
    // "auto" is not a mode, so it resolves to dark here; a terminal probe would do better and
    // this app does not have one yet.
    const int named = rolltui_theme_mode_from_name(w->mode.p ? w->mode.p : "", w->mode.n);
    const int mode = named >= 0 ? named : ROLLTUI_MODE_DARK;
    // The SESSION's vocabulary, so a person's theme may map this app's states by name.
    RolltuiEffectMap* eff = rolltui_theme_load(w->colours, mode, rolltui_theme_vocab(ctx), got, &name, &rep);
    if (eff) {
      std::copy(std::begin(got), std::end(got), styles);
      rolltui_effect_map_free(effects);
      effects = eff;
      merge_effects();
      RolltuiScrollbarGlyphs g;
      rolltui_theme_scrollbar_glyphs(w->colours, &g);
      rolltui_context_set_scrollbar_glyphs(ctx, &g);
    }
    rolltui_str_free(&name);
    rolltui_theme_report_release(&rep);
    rolltui_preset_store_value_free(theme_store, w);
  }

  // THE PICKER IS THE LIBRARY'S, reached through the window calls by its content string. This
  // app tells it where to start, what its settings are, and reads what its keys said; the
  // columns, the anchor, the fade, the dividers and the memory are the kind's.
  bool picker_started = false;
  void picker_go(const std::string& path) { rolltui_windows_set_picker_dir(windows, kPicker, 10, path.data(), path.size()); }
  void apply_picker_options() {
    RolltuiPickerOptions o{};
    rolltui_picker_options_init(&o);
    o.hidden = opt.hidden ? 1 : 0;
    o.sort = opt.sort == Sort::Size ? ROLLTUI_SORT_SIZE : opt.sort == Sort::Modified ? ROLLTUI_SORT_MODIFIED : ROLLTUI_SORT_NAME;
    o.motion = opt.motion ? 1 : 0;
    o.dividers = opt.dividers ? 1 : 0;
    o.take_folders = 1;  // a directory picker: Enter on a folder CHOOSES it, and Right enters it
    rolltui_windows_set_picker_options(windows, kPicker, 10, &o);
  }

  static const std::vector<std::string>& help_scopes() {
    static const std::vector<std::string> s = {"app", "picker", "input", "menu", "stack"};
    return s;
  }

  // ---- THE SETTINGS FILE: what a person chose, kept between runs ------------------------------
  // `<config>/rolltui/dirktui/settings.json`, three fields, written whole on every change and
  // read once at start through rung 3 of the app's files. Not the theme store: these are this
  // app's own facts (sort, dotfiles, motion), and a theme is a look shared by every host.
  static std::string settings_dir() { return user_presets_dir() + "/dirktui"; }
  static const char* sort_name(Sort s) { return s == Sort::Name ? "name" : s == Sort::Size ? "size" : "modified"; }
  void load_settings(const char* argv0) {
    RolltuiStr t{};
    if (rolltui_app_file(argv0, "dirktui", "settings", nullptr, 0, &t, nullptr)) {
      RolltuiStr err{};
      if (RolltuiJsonValue* root = rolltui_json_parse(t.p ? t.p : "", t.n, &err)) {
        opt.motion = rolltui_json_as_bool(rolltui_json_get(root, "motion", 6), 1) != 0;
        opt.dividers = rolltui_json_as_bool(rolltui_json_get(root, "dividers", 8), 1) != 0;
        opt.hidden = rolltui_json_as_bool(rolltui_json_get(root, "hidden", 6), 1) != 0;
        std::size_t n = 0;
        const char* sv = rolltui_json_as_string(rolltui_json_get(root, "sort", 4), "name", 4, &n);
        const std::string sort(sv, n);
        opt.sort = sort == "size" ? Sort::Size : sort == "modified" ? Sort::Modified : Sort::Name;
        opt.leave = rolltui_json_as_bool(rolltui_json_get(root, "leave", 5), 0) != 0;
        const char* lv = rolltui_json_as_string(rolltui_json_get(root, "land", 4), "start", 5, &n);
        opt.land_in_file_folder = std::string(lv, n) == "file";
        const char* cv = rolltui_json_as_string(rolltui_json_get(root, "paths", 5), "absolute", 8, &n);
        opt.copy_relative = std::string(cv, n) == "relative";
        opt.open_with.clear();
        if (const RolltuiJsonValue* open = rolltui_json_get(root, "open", 4))  // Null when absent: every lookup below is then empty
          for (const TypeGroup& g : kGroups) {
            const char* pv = rolltui_json_as_string(rolltui_json_get(open, g.id, std::strlen(g.id)), "", 0, &n);
            if (n) opt.open_with[g.id] = std::string(pv, n);
          }
        rolltui_json_free(root);
      } else {
        std::fprintf(stderr, "dirktui: settings file: %s (defaults kept)\n", err.c_str());
      }
      rolltui_str_free(&err);
    }
    rolltui_str_free(&t);
  }
  void save_settings() {
    const std::string dir = settings_dir();
    std::string made;
    for (std::size_t i = 1; i <= dir.size(); ++i)
      if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    std::ofstream out(dir + "/settings.json", std::ios::binary | std::ios::trunc);
    out << "{ \"motion\": " << (opt.motion ? "true" : "false") << ", \"dividers\": " << (opt.dividers ? "true" : "false")
        << ", \"hidden\": " << (opt.hidden ? "true" : "false")
        << ", \"sort\": \"" << sort_name(opt.sort) << "\", \"leave\": " << (opt.leave ? "true" : "false")
        << ", \"land\": \"" << (opt.land_in_file_folder ? "file" : "start")
        << "\", \"paths\": \"" << (opt.copy_relative ? "relative" : "absolute") << "\", \"open\": {";
    bool first = true;
    for (const auto& [group, prog] : opt.open_with) {
      out << (first ? " " : ", ") << '"' << group << "\": \"" << prog << '"';
      first = false;
    }
    out << (first ? "" : " ") << "} }\n";
    if (!out) hint = "could not write " + dir + "/settings.json";
  }

  // The settings popup's boxes, set from the live values the moment the popup exists — which is
  // after the frame's sync has built its widget, so this runs from `prepare()`. The window is
  // found through the FOCUS, never by a name this source would otherwise have to carry.
  void sync_menu() {
    const RolltuiLayoutNode* n = rolltui_window_stack_focused(stack);
    std::size_t len = 0;
    const char* id = n ? rolltui_layout_node_id(n, &len) : nullptr;
    RolltuiMenu* m = id ? rolltui_windows_menu_at(windows, id, len) : nullptr;
    if (!m) return;
    rolltui_menu_set_value(m, "sort", 4, sort_name(opt.sort), std::strlen(sort_name(opt.sort)));
    rolltui_menu_set_checked(m, "hidden", 6, opt.hidden ? 1 : 0);
    rolltui_menu_set_checked(m, "motion", 6, opt.motion ? 1 : 0);
    rolltui_menu_set_checked(m, "dividers", 8, opt.dividers ? 1 : 0);
    rolltui_menu_set_checked(m, "leave", 5, opt.leave ? 1 : 0);
    rolltui_menu_set_checked(m, "land", 4, opt.land_in_file_folder ? 1 : 0);
    rolltui_menu_set_checked(m, "relative", 8, opt.copy_relative ? 1 : 0);
    // THE "OPEN WITH" CHOICES ARE FILLED HERE, not in the file: their options are what this
    // machine has. The skeleton (one choice per type group) is the file's; the contents are
    // what `detect_programs` found, the same move roll makes with its preset listings.
    for (const TypeGroup& g : kGroups) {
      const std::string id = std::string("open_") + g.id;
      RolltuiMenuItemList options{};
      for (const auto& [pid, label] : options_for(g)) {
        RolltuiMenuItem* o = rolltui_menu_list_add(&options);
        rolltui_menu_item_set(o, ROLLTUI_MENU_ACTION, pid.c_str(), pid.size(), label.c_str(), label.size(), nullptr, 0);
      }
      rolltui_menu_set_options(m, id.c_str(), id.size(), &options);
      rolltui_menu_list_release(&options);
      const std::string cur = program_for(g);
      rolltui_menu_set_value(m, id.c_str(), id.size(), cur.c_str(), cur.size());
    }
  }
  // What the menu offers for a group: the installed programs in the group's own order, the
  // system opener, then the command line — and what is chosen, which is the setting when it is
  // still on offer and the first offer otherwise.
  std::set<std::string> installed;  // program ids found on this machine
  std::vector<std::pair<std::string, std::string>> options_for(const TypeGroup& g) const {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(g.prefer);
    std::string id;
    while (in >> id) {
      if (id == kSystemProgram) continue;
      if (const Program* p = program_named(id); p && installed.count(id)) out.emplace_back(id, p->label);
    }
    out.emplace_back(kSystemProgram, "the system opener");
    out.emplace_back(kShellProgram, "the command line (not opened)");
    return out;
  }
  std::string program_for(const TypeGroup& g) const {
    const auto offered = options_for(g);
    if (const auto it = opt.open_with.find(g.id); it != opt.open_with.end())
      for (const auto& [pid, label] : offered) if (pid == it->second) return pid;
    // the default: the first of the group's preferences that is installed; `system` is always
    std::istringstream in(g.prefer);
    std::string id;
    while (in >> id) if (id == kSystemProgram || installed.count(id)) return id;
    return kSystemProgram;
  }
  static std::string program_label(const std::string& id) {
    if (const Program* p = program_named(id)) return p->label;
    return id == kSystemProgram ? "the system opener" : id;
  }

  void mount() {
    if (!menu_json.empty()) rolltui_context_add_menu(ctx, "places", 6, menu_json.data(), menu_json.size());
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
    RolltuiStr sel{};
    int is_dir = 0;
    if (!rolltui_windows_picker_selected(a.windows, kPicker, 10, &sel, &is_dir) || sel.n == 0) {
      rolltui_rows_add(out, "entry", 5, "(none)", 6);
      rolltui_str_free(&sel);
      return;
    }
    const std::string path(sel.p, sel.n);
    rolltui_str_free(&sel);
    // The facts are the file's, read here: the picker answers with a PATH, and what a path
    // is on disk is one stat away.
    struct stat st {};
    const bool known = lstat(path.c_str(), &st) == 0;
    const std::size_t slash = path.find_last_of('/');
    const std::string name = path == "/" ? path : path.substr(slash == std::string::npos ? 0 : slash + 1);
    rolltui_rows_add(out, "name", 4, name.data(), name.size());
    rolltui_rows_add(out, "folder", 6, path.data(), path.size());
    const char* kind = !known ? "unreadable" : is_dir ? "directory" : "file";
    rolltui_rows_add(out, "kind", 4, kind, std::strlen(kind));
    const std::string size = is_dir || !known ? std::string("-") : human_size(static_cast<long long>(st.st_size));
    rolltui_rows_add(out, "size", 4, size.data(), size.size());
    const std::string when = stamp(known ? static_cast<long long>(st.st_mtime) : 0);
    rolltui_rows_add(out, "modified", 8, when.data(), when.size());
    const std::string perm = permissions(known ? static_cast<unsigned>(st.st_mode) : 0u);
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
    root = path;
    picker_go(path);
    hint = "at " + path;
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

  // How this terminal draws East Asian AMBIGUOUS glyphs. Not a preference and not a test hook:
  // it is a fact about the terminal the process cannot yet ask for, and a widget that guesses it
  // wrong cuts a two-cell glyph into one column.
  int ambiguous = 0;

  void prepare() {
    // RE-RESOLVE ONLY WHEN THE STORE MOVED. An edit made in the theme editor bumps the store's
    // version, and a frame that draws the old styles would make the editor look broken. A
    // version compare rather than a deep one, so an unchanged frame costs nothing.
    if (theme_store) {
      const unsigned long long v = rolltui_preset_store_version(theme_store);
      if (v != theme_seen) { theme_seen = v; sync_theme(); }
    }
    const RolltuiWidgetEnv env{static_cast<unsigned char>(ambiguous), now_ms};
    rolltui_context_set_env(ctx, &env);
    rolltui_context_set_bindings(ctx, bindings);
    rolltui_windows_sync(windows, stack);
    // The picker exists once the sync built it: its settings every frame (cheap, and it re-reads
    // only when a setting that changes the listing moved) and its start once.
    apply_picker_options();
    if (!picker_started) { picker_go(root); picker_started = true; }
    if (menu_dirty) { sync_menu(); menu_dirty = false; }
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

  // Once after each batch of events: did the browser end the session, or ask for a copy?
  // An accept on a FOLDER (or on an empty column, which is what the eye is on) leaves with its
  // path. An accept on a FILE: to the command line when `leave` says so, when it is executable,
  // when its type is nobody's, or when the type's program is "the command line"; otherwise it
  // opens with that program and the cursor stays. THE EXIT STATUS IS THE VERB: 0 means "go
  // there", 3 means "put this on the command line", 4 "beside it, then ./name"; a path's shape
  // is never read to guess which.
  void settle() {
    RolltuiPickerEvent ev{};
    if (!rolltui_windows_picker_event(windows, kPicker, 10, &ev)) return;
    const std::string path(ev.path.p ? ev.path.p : "", ev.path.n);
    if (ev.kind == ROLLTUI_PICKER_EVENT_TAKEN) {
      struct stat st {};
      const bool folder = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
      const std::size_t slash = path.find_last_of('/');
      const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
      if (folder || path.empty()) { chosen = path; exit_code = 0; quit = true; }
      else if (opt.leave || is_executable(path)) to_command_line(path);
      else {
        const TypeGroup* g = group_of(name);
        const std::string prog = g ? program_for(*g) : std::string(kShellProgram);
        if (prog == kShellProgram) to_command_line(path);
        else if (open_with(prog, path)) hint = "opened " + name + " with " + program_label(prog);
        else hint = "could not open " + name + " with " + program_label(prog);
      }
    } else if (ev.kind == ROLLTUI_PICKER_EVENT_CANCELLED) {
      quit = true;
    } else if (ev.kind == ROLLTUI_PICKER_EVENT_COPY) {
      const bool relative = opt.copy_relative != (ev.inverse != 0);  // the Option chord inverts the setting
      const std::string text = relative ? relative_to_start(path) : path;
      hint = copy_to_clipboard(text) ? "copied " + text : "could not copy: no clipboard command";
    }
    rolltui_picker_event_release(&ev);
  }

  // What the process leaves behind on stdout: the chosen path and a newline when the status
  // hands the shell something (0: go there; 3: put it on the line), or nothing at all. Called
  // after the terminal is restored, so the line lands on a normal screen.
  int finish() const {
    if (exit_code == 0 || exit_code == 3 || exit_code == 4) { std::fwrite(chosen.data(), 1, chosen.size(), stdout); std::fputc('\n', stdout); }
    return exit_code;
  }

  // ---- what a file IS, for Enter --------------------------------------------------------------
  // An executable is never handed to an opener: the failure that must not happen — a script run
  // by the program that was meant to show it — is decided by the file's own bit, not by a list
  // of names. What a document opens with is `group_of` + `program_for`.
  static bool is_executable(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(path.c_str(), X_OK) == 0;
  }
  // A file to the command line: exit 3 with the path as the path setting says (from where dirk
  // started), or exit 4 with the file's absolute path for the shell to `cd` beside and type
  // `./name` — the setting `land` decides which.
  void to_command_line(const std::string& path) {
    if (opt.land_in_file_folder) { chosen = path; exit_code = 4; }
    else { chosen = opt.copy_relative ? relative_to_start(path) : path; exit_code = 3; }
    quit = true;
  }

  // ---- where dirk started, and paths said from there ------------------------------------------
  std::string start_dir;  // absolute, set once at start
  static std::vector<std::string> parts(const std::string& p) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : p) {
      if (c == '/') { if (!cur.empty()) v.push_back(cur); cur.clear(); }
      else cur += c;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
  }
  // `path` as a relative path from `start_dir`, always with a leading `./` or `../` so it reads
  // as a place and never as a command.
  std::string relative_to_start(const std::string& path) const {
    const std::vector<std::string> a = parts(start_dir), b = parts(path);
    std::size_t common = 0;
    while (common < a.size() && common < b.size() && a[common] == b[common]) ++common;
    std::string out;
    for (std::size_t i = common; i < a.size(); ++i) out += out.empty() ? ".." : "/..";
    for (std::size_t i = common; i < b.size(); ++i) out += (out.empty() ? "" : "/") + b[i];
    if (out.empty()) return ".";
    return out.rfind("..", 0) == 0 ? out : "./" + out;
  }

  // THE SYSTEM OPENER: the platform's. A stand-in (`$DIRK_OPEN`) is put in front of the whole
  // command by `open_with`, never substituted here, so the stand-in sees "open <path>".
  static const char* opener() {
#ifdef __APPLE__
    return "open";
#else
    return "xdg-open";
#endif
  }
  // A HEADLESS RUN REACHES NO REAL OPENER AND NO REAL CLIPBOARD. A `--frame` run is a test, and
  // a test that presses Enter on a file must not put a window on someone's screen or a path on
  // their clipboard: without a stand-in named in the environment, both are refused and said.
  static bool headless;
  // THE TERMINAL AND THE SCREEN, for a program that takes the terminal over: set by main once
  // there is a terminal; NULL in a headless run, where such a program is never launched.
  RolltuiTerminal* term = nullptr;
  RolltuiSwap* swap = nullptr;
  int tty_fd = -1;
  // Opens `path` with the program `id`. A program that takes the terminal over runs on this
  // process's own tty with dirktui stepped aside, and the screen is redrawn whole when it
  // returns; any other returns at once (`open`, `code`, a stand-in). With `$DIRK_OPEN` set the
  // stand-in is run INSTEAD, handed the command it would have been — so a test sees "nvim
  // <path>" without a Neovim, and nothing takes the terminal over.
  bool open_with(const std::string& id, const std::string& path) {
    const char* stand_in = std::getenv("DIRK_OPEN");
    if (stand_in && !*stand_in) stand_in = nullptr;
    if (headless && !stand_in) return false;
    std::vector<std::string> cmd;
    const Program* p = program_named(id);
    if (id == kSystemProgram) cmd = {opener(), path};
    else if (!p) return false;
    else if (p->mac_app) cmd = {"open", "-a", p->exe, path};
    else {
      cmd = {p->exe};
      if (p->arg) cmd.push_back(p->arg);
      cmd.push_back(path);
    }
    const bool takes_terminal = p && p->terminal && !stand_in;
    if (stand_in) cmd.insert(cmd.begin(), stand_in);
    if (takes_terminal && (!term || tty_fd < 0)) return false;
    std::vector<const char*> argv;
    for (const std::string& a : cmd) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (takes_terminal) {
      // The editor gets the terminal itself, not the pipe `$(dirk)` is reading this process's
      // stdout through: all three streams on the tty.
      posix_spawn_file_actions_adddup2(&fa, tty_fd, 0);
      posix_spawn_file_actions_adddup2(&fa, tty_fd, 1);
      posix_spawn_file_actions_adddup2(&fa, tty_fd, 2);
      rolltui_terminal_suspend(term);
    } else {
      posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
      posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
      posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    }
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, const_cast<char* const*>(argv.data()), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc == 0) waitpid(pid, nullptr, 0);
    if (takes_terminal) {
      rolltui_terminal_resume(term);
      if (swap) rolltui_swap_invalidate(swap);  // the editor's screen is gone; draw ours whole
    }
    return rc == 0;
  }
  // THE CLIPBOARD: `$DIRK_CLIPBOARD`, else the platform's, fed the text on stdin.
  static bool copy_to_clipboard(const std::string& text) {
    const char* cmd = std::getenv("DIRK_CLIPBOARD");
    if (headless && !cmd) return false;
    if (!cmd || !*cmd) {
#ifdef __APPLE__
      cmd = "pbcopy";
#else
      cmd = "wl-copy 2>/dev/null || xclip -selection clipboard";
#endif
    }
    FILE* p = popen(cmd, "w");
    if (!p) return false;
    std::fwrite(text.data(), 1, text.size(), p);
    return pclose(p) == 0;
  }

  void toggle_popup(const char* id) {
    const std::size_t len = std::strlen(id);
    if (rolltui_window_stack_depth(stack) > 1) { rolltui_window_stack_pop(stack); return; }
    rolltui_window_stack_push_popup(stack, layout, id, len);
  }

  void run_action(const std::string& action) {
    if (action == "app.quit") quit = true;
    else if (action == "app.jump") {
      // WALL 4 (phase file): `rolltui_window_stack_focus` takes a window ID, so an app that
      // wants to put the cursor in its own input must NAME a window the layout owns. roll does
      // the same for its find bar. There is no focus-by-CONTENT, which is what a host actually
      // knows — it bound `input:path`, it did not choose the id.
      rolltui_window_stack_focus(stack, "where", 5);
      hint = "type a path";
    }
    // A panel this screen declares is the library's to open — see
    // `rolltui_window_stack_action_popup`. `details`, `help`, `theme` and `keys` are four lines
    // this file does not have.
    else if (rolltui_window_stack_action_popup(stack, layout, action.data(), action.size())) {
      if (action == "app.menu") menu_dirty = true;
    }
    else if (action == "app.hidden") set_hidden(!opt.hidden);
    else if (action == "app.sort") set_sort(opt.sort == Sort::Name ? Sort::Size : opt.sort == Sort::Size ? Sort::Modified : Sort::Name);
  }

  // The three settings, each changed in ONE place whether a chord or the menu asked, and saved.
  void set_hidden(bool on) {
    opt.hidden = on;
    apply_picker_options();
    hint = opt.hidden ? "dotfiles shown" : "dotfiles hidden";
    save_settings();
  }
  void set_sort(Sort s) {
    opt.sort = s;
    apply_picker_options();
    hint = std::string("sorted by ") + sort_name(opt.sort);
    save_settings();
  }
  void set_motion(bool on) {
    opt.motion = on;
    apply_picker_options();
    hint = on ? "motion on" : "motion off";
    save_settings();
  }

  void handle(const RolltuiEvent& e) {
    // The moment an event lands is this frame's: the widgets read the clock from the env.
    const RolltuiWidgetEnv env{static_cast<unsigned char>(ambiguous), now_ms};
    rolltui_context_set_env(ctx, &env);
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
    // The settings menu is the host's to drive, BEFORE the window table sees the event — the
    // menu widget would otherwise consume the key and the host would never learn what was chosen.
    if (RolltuiMenu* m = rolltui_windows_menu_at(windows, target.data(), target.size())) {
      RolltuiMenuEvent ev{};
      rolltui_menu_handle(m, &e, bindings, rolltui_menu_default_actions(), &ev);
      const std::string id(ev.id.p ? ev.id.p : "", ev.id.n);
      if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && id == "sort") {
        const std::string v(ev.value.p ? ev.value.p : "", ev.value.n);
        set_sort(v == "size" ? Sort::Size : v == "modified" ? Sort::Modified : Sort::Name);
      } else if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && id.rfind("open_", 0) == 0) {
        opt.open_with[id.substr(5)] = std::string(ev.value.p ? ev.value.p : "", ev.value.n);
        hint = id.substr(5) + " opens with " + program_label(opt.open_with[id.substr(5)]);
        save_settings();
      } else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "leave") {
        opt.leave = ev.checked != 0;
        hint = opt.leave ? "enter on a file: leave with it on the command line" : "enter on a file: open it, stay here";
        save_settings();
      } else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "land") {
        opt.land_in_file_folder = ev.checked != 0;
        hint = opt.land_in_file_folder ? "command line: in the file's folder, ./name" : "command line: where dirk started";
        save_settings();
      } else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "relative") {
        opt.copy_relative = ev.checked != 0;
        hint = opt.copy_relative ? "paths: relative to where dirk started" : "paths: absolute";
        save_settings();
      } else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "hidden") set_hidden(ev.checked != 0);
      else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "motion") set_motion(ev.checked != 0);
      else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "dividers") {
        opt.dividers = ev.checked != 0;
        hint = opt.dividers ? "column dividers on" : "column dividers off";
        save_settings();
      }
      rolltui_menu_event_release(&ev);
      return;
    }
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
    RolltuiPickerStatus ps{};
    const bool have_picker = rolltui_windows_picker_status(windows, kPicker, 10, &ps) != 0;
    char num[64];
    status_rows.reset();
    // The start folder, with the home directory as `~` so the keys beside it are not pushed off
    // a narrow screen by a long path.
    const char* home = std::getenv("HOME");
    const std::string shown = home && *home && root.rfind(home, 0) == 0 && (root.size() == std::strlen(home) || root[std::strlen(home)] == '/')
                                  ? "~" + root.substr(std::strlen(home)) : root;
    rolltui_rows_add(&status_rows, "", 0, shown.data(), shown.size());
    if (keys_hint.empty()) build_keys_hint();
    rolltui_rows_add(&status_rows, "", 0, keys_hint.data(), keys_hint.size());
    // A REPORT OUTRANKS EVERY FACT BELOW IT: the line is truncated from the right, so anything
    // that must be read goes before anything that is merely useful.
    if (!note.empty()) rolltui_rows_add(&status_rows, "", 0, note.data(), note.size());
    // A DATA failure said the way the person who caused it will read it. The window report above
    // is the app author's channel and names a window and a content string; someone who mistyped a
    // path needs the path back, not the plumbing that carried it.
    if (have_picker && ps.error.n) rolltui_rows_add(&status_rows, "", 0, ps.error.p, ps.error.n);
    if (!hint.empty()) rolltui_rows_add(&status_rows, "", 0, hint.data(), hint.size());
    if (have_picker) {
      std::snprintf(num, sizeof num, "%zu", ps.entries);
      status_rows.add("entries", num);
      std::snprintf(num, sizeof num, "%zu/%zu", ps.column, ps.columns);
      status_rows.add("column", num);
      status_rows.add("sort", opt.sort == Sort::Name ? "name" : opt.sort == Sort::Size ? "size" : "modified");
      if (opt.hidden) rolltui_rows_add(&status_rows, "", 0, "+dotfiles", 9);
    }
    rolltui_picker_status_release(&ps);
    rolltui_frame_put_fields(f, draw_scratch, 1, h - 1, &status_rows, style(ROLLTUI_ROLE_LABEL),
                             style(ROLLTUI_ROLE_VALUE), w - 1, 0);
    apply_effects(f);
  }

  // The one line every host has: after the whole screen composed and before the diff, the
  // theme's motion is applied to whatever was marked. Also run for a headless frame, so a
  // self-test can read what a tick touched.
  void apply_effects(RolltuiFrame* f) {
    last_fx = RolltuiEffectReport{};
    last_marks = rolltui_frame_mark_count(f);
    if (!opt.motion || last_marks == 0 || !effects || rolltui_effect_map_empty(effects)) return;
    rolltui_effects_apply(ctx, f, effect_scratch, styles, nullptr, effects, now_ms, ambiguous, &last_fx, nullptr, nullptr);
  }
};

bool App::headless = false;

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
extern const RolltuiEmbeddedFile dirktui_kAppFiles[];
extern const size_t dirktui_kAppFileCount;
}

// Where a person's own presets live, the same three rungs `rolltui_app_file` walks for an app's
// own files: an explicit configuration directory, then the XDG one, then the home default.
std::string user_presets_dir() {
  if (const char* d = std::getenv("ROLL_CONFIG_DIR"); d && *d) return std::string(d) + "/rolltui";
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return std::string(x) + "/roll/rolltui";
  const char* home = std::getenv("HOME");
  return std::string(home && *home ? home : ".") + "/.config/roll/rolltui";
}

RolltuiLayout* load_layout_text(RolltuiContext* ctx, const std::string& text, RolltuiLayoutReport* rep) {
  std::size_t defaults_n = 0;
  const RolltuiLayoutAction* defaults = rolltui_layout_shipped_default_actions(ctx, &defaults_n);
  return rolltui_load_layout_text(text.data(), text.size(), defaults, defaults_n,
                                  rolltui_layout_default_hooks(), rep);
}

[[maybe_unused]] bool parse_size(const std::string& s, int& w, int& h) {
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
               "usage: dirktui [PATH] [--ambiguous-wide]   browse from PATH (default: the current directory)\n"
               "                                           Enter on a folder prints it on stdout and exits 0;\n"
               "                                           on a file: a document opens with the program chosen\n"
               "                                           for its type (F2: from what is installed); a script,\n"
               "                                           a binary or an unknown type goes to the command line;\n"
               "                                           exit 3: put the printed path on the command line;\n"
               "                                           exit 4: into the printed file's folder, then ./name\n"
               "                                           Esc prints nothing and exits 1\n"
               "       dirktui install [zsh|bash|fish]     link this binary into ~/.local/bin and put the shell\n"
               "                                           side into the shell's rc file ($SHELL's by default);\n"
               "                                           `dirktui uninstall` takes both out again\n"
               "       dirktui init zsh|bash|fish          the shell side: a `dirk` function and Right Arrow\n"
               "                                           zsh:  eval \"$(dirktui init zsh)\"    (bash likewise)\n"
               "                                           fish: dirktui init fish | source\n"
#ifdef ROLLTUI_SELFTEST
               "       [--presets DIR] [--layout NAME|FILE] [--theme NAME]\n"
               "       [--frame WxH] [--keys \"Down Right CtrlD\"] [--apps DIR[:DIR]]\n"
#endif
               );
  return 2;
}

// THE SHELL SIDE, printed by `dirktui init <shell>` so it is versioned with the binary it drives
// (zoxide, atuin and fzf all ship their shell code this way). The three scripts say the same
// thing in three dialects; what they say, so a reader of the C++ knows the other half:
//   * A CHOSEN PATH MEANS ONE COMMAND LINE: a folder is `cd`'d into; a file means `cd` to its
//     folder (opening is the BINARY's, by its own setting). The line is composed once
//     (`_dirk_command`) and is what runs, what goes into history, and what a person sees.
//     Exit status 3 carries a different verb: the printed path goes onto the command line —
//     inserted at the cursor from the widget, pushed as the next line from `dirk` at a prompt
//     where the shell can (`print -z` in zsh), and printed where it cannot.
//   * `dirk [PATH]` at a prompt browses, then runs that line. `dirk init …` passes through.
//   * Right Arrow with the cursor at the END of the line opens the browser. On an empty line the
//     composed command runs — in zsh through accept-line (fzf's alt-c), in bash through
//     `history -s` + eval, in fish directly — so it is in history where the shell allows. On a
//     line with text the choice is inserted at the cursor, quoted, starting from and replacing
//     the last word when that word names a folder (`ls src/<Right>`). Fish keeps its own Right
//     on a non-empty line, because a pending autosuggestion cannot be asked about there.
//   * Right Arrow anywhere else, or with an autosuggestion showing, is what it always was.
constexpr const char* kZshInit = R"zsh(# dirk — zsh integration for dirktui. In ~/.zshrc:   eval "$(dirktui init zsh)"

# A chosen path as the ONE command line that acts on it: the line that runs and the line history
# keeps. A folder is entered; a file means its folder (opening a file is the binary's, by its
# own setting).
_dirk_command() {
  local out="$1"
  if [[ -d "$out" ]]; then print -r -- "builtin cd -- ${(q)out}"; else print -r -- "builtin cd -- ${(q)out:h}"; fi
}

dirk() {
  case "$1" in init|install|uninstall) command dirktui "$@"; return $?;; esac
  local out rc
  out="$(command dirktui "$@")"; rc=$?
  if (( rc == 4 )); then   # into the file's folder, then ./name onto the line
    builtin cd -- "${out:h}" || return 1
    out="./${out:t}"; rc=3
  fi
  if (( rc == 3 )); then   # onto the next command line where there is one; shown where there is not
    if [[ -o zle ]]; then print -z -- "$out"; else print -r -- "$out"; fi
    return 0
  fi
  (( rc == 0 )) || return $rc
  [[ -n "$out" ]] || return 1
  eval "$(_dirk_command "$out")"
}

_dirk_widget() {
  emulate -L zsh
  local start='' word='' out
  if [[ -n "$LBUFFER" ]]; then
    word="${LBUFFER##* }"
    local probe="$word"
    [[ "$probe" == '~' || "$probe" == '~/'* ]] && probe="$HOME${probe#\~}"
    [[ -n "$probe" && -d "$probe" ]] && start="$probe"
  fi
  local rc
  out="$(command dirktui ${start:+"$start"} < /dev/tty)"; rc=$?
  if (( rc == 4 )) && [[ -n "$out" ]]; then   # exit 4: into the file's folder, then ./name onto the line
    builtin cd -- "${out:h}" && out="./${out:t}" && rc=3
  fi
  if (( rc == 3 )) && [[ -n "$out" ]]; then   # exit 3: onto the command line, wherever the cursor is
    [[ -n "$start" ]] && LBUFFER="${LBUFFER%"$word"}"
    LBUFFER+="${(q)out}"
    zle reset-prompt
    return 0
  fi
  if (( rc != 0 )) || [[ -z "$out" ]]; then
    zle redisplay
    return 0
  fi
  if [[ -z "$BUFFER" ]]; then
    zle push-line
    BUFFER="$(_dirk_command "$out")"
    zle accept-line
    local ret=$?
    zle reset-prompt
    return $ret
  fi
  [[ -n "$start" ]] && LBUFFER="${LBUFFER%"$word"}"
  LBUFFER+="${(q)out}"
  zle reset-prompt
}
zle -N _dirk_widget

_dirk_forward_char() {
  if (( CURSOR < ${#BUFFER} )) || [[ -n "$POSTDISPLAY" ]]; then
    zle forward-char
  else
    zle _dirk_widget
  fi
}
zle -N _dirk_forward_char
bindkey -M emacs '^[[C' _dirk_forward_char
bindkey -M emacs '^[OC' _dirk_forward_char
bindkey -M viins '^[[C' _dirk_forward_char
bindkey -M viins '^[OC' _dirk_forward_char
)zsh";

constexpr const char* kBashInit = R"bash(# dirk — bash integration for dirktui. In ~/.bashrc:   eval "$(dirktui init bash)"

# A chosen path as the ONE command line that acts on it: the line that runs and the line history
# keeps. A folder is entered; a file means its folder (opening a file is the binary's, by its
# own setting).
_dirk_command() {
  local out="$1"
  if [[ -d "$out" ]]; then printf 'builtin cd -- %q' "$out"; else printf 'builtin cd -- %q' "$(dirname -- "$out")"; fi
}

dirk() {
  case "$1" in init|install|uninstall) command dirktui "$@"; return $?;; esac
  local out rc
  out="$(command dirktui "$@")"; rc=$?
  if (( rc == 4 )); then builtin cd -- "$(dirname -- "$out")" || return 1; out="./$(basename -- "$out")"; rc=3; fi
  if (( rc == 3 )); then printf '%s\n' "$out"; return 0; fi   # bash cannot push a next line from a command: shown instead
  (( rc == 0 )) || return $rc
  [[ -n "$out" ]] || return 1
  eval "$(_dirk_command "$out")"
}

# READLINE_POINT is a BYTE offset into READLINE_LINE; lengths compared to it are measured in bytes.
_dirk_bytes() { local LC_ALL=C; printf '%s' "${#1}"; }

_dirk_forward_char() {
  local len
  len="$(_dirk_bytes "$READLINE_LINE")"
  if (( READLINE_POINT < len )); then
    # One CHARACTER forward, however many bytes it is.
    local head next
    head="$(LC_ALL=C; printf '%s' "${READLINE_LINE:0:READLINE_POINT}")"
    next="${READLINE_LINE:${#head}:1}"
    READLINE_POINT=$(( READLINE_POINT + $(_dirk_bytes "$next") ))
    return 0
  fi
  local start='' word='' out
  if [[ -n "$READLINE_LINE" ]]; then
    word="${READLINE_LINE##* }"
    local probe="$word"
    [[ "$probe" == '~' || "$probe" == '~/'* ]] && probe="$HOME${probe#\~}"
    [[ -n "$probe" && -d "$probe" ]] && start="$probe"
  fi
  local rc
  out="$(command dirktui ${start:+"$start"} < /dev/tty)"; rc=$?
  if (( rc == 4 )) && [[ -n "$out" ]]; then   # exit 4: into the file's folder, then ./name onto the line
    builtin cd -- "$(dirname -- "$out")" && out="./$(basename -- "$out")" && rc=3
  fi
  if (( rc == 3 )) && [[ -n "$out" ]]; then   # exit 3: onto the command line
    [[ -n "$start" ]] && READLINE_LINE="${READLINE_LINE%"$word"}"
    READLINE_LINE+="$(printf '%q' "$out")"
    READLINE_POINT="$(_dirk_bytes "$READLINE_LINE")"
    return 0
  fi
  (( rc == 0 )) || return 0
  [[ -n "$out" ]] || return 0
  if [[ -z "$READLINE_LINE" ]]; then
    local cmd
    cmd="$(_dirk_command "$out")"
    history -s "$cmd"
    eval "$cmd"
    return 0
  fi
  [[ -n "$start" ]] && READLINE_LINE="${READLINE_LINE%"$word"}"
  READLINE_LINE+="$(printf '%q' "$out")"
  READLINE_POINT="$(_dirk_bytes "$READLINE_LINE")"
}
bind -m emacs-standard -x '"\e[C": _dirk_forward_char'
bind -m emacs-standard -x '"\eOC": _dirk_forward_char'
bind -m vi-insert -x '"\e[C": _dirk_forward_char'
bind -m vi-insert -x '"\eOC": _dirk_forward_char'
)bash";

constexpr const char* kFishInit = R"fish(# dirk — fish integration for dirktui. In config.fish:   dirktui init fish | source

# What a chosen path means: a folder is entered; a file means its folder (opening a file is the
# binary's, by its own setting).
function _dirk_go --argument-names out
    if test -d "$out"
        builtin cd -- "$out"
    else
        builtin cd -- (dirname -- "$out")
    end
end

function dirk
    if contains -- "$argv[1]" init install uninstall
        command dirktui $argv
        return
    end
    set -l out (command dirktui $argv)
    set -l rc $status
    if test $rc -eq 4
        builtin cd -- (dirname -- "$out"); or return 1
        set out "./"(basename -- "$out")
        set rc 3
    end
    if test $rc -eq 3
        printf '%s\n' "$out"   # fish cannot push a next line from a command: shown instead
        return 0
    end
    test $rc -eq 0; or return $rc
    test -n "$out"; or return 1
    _dirk_go "$out"
end

# Right Arrow opens the browser only on an EMPTY line. With text on the line fish may be showing
# an autosuggestion, which cannot be asked about here, so Right stays fish's own.
function _dirk_forward_char
    if test -n (commandline)
        commandline -f forward-char
        return
    end
    set -l out (command dirktui </dev/tty)
    set -l rc $status
    commandline -f repaint
    test -n "$out"; or return
    if test $rc -eq 4
        builtin cd -- (dirname -- "$out"); and set out "./"(basename -- "$out"); and set rc 3
    end
    if test $rc -eq 3
        commandline -i -- (string escape -- $out)   # exit 3: onto the command line
        return
    end
    test $rc -eq 0; or return
    _dirk_go "$out"
    commandline -f repaint
end
bind \e\[C _dirk_forward_char
bind \eOC _dirk_forward_char
)fish";

// `dirktui init <shell>`: the integration for that shell on stdout. A name this binary has no
// script for is refused with the list it has, never answered with another shell's.
// ---- `dirktui install [zsh|bash|fish]` and `uninstall` --------------------------------------
// Puts the binary where a shell finds it and the shell side where the shell reads it, ONCE:
//   * `~/.local/bin/dirktui` — a SYMLINK to this very binary, so the next build is what runs;
//     a link pointing elsewhere is replaced, a real file there is left alone and named;
//   * the shell's rc file gets one MARKED block: PATH gains ~/.local/bin (guarded), and the
//     shell side is evaluated from the binary — zsh: ~/.zshrc, bash: ~/.bashrc, fish:
//     ~/.config/fish/conf.d/dirk.fish (fish sources conf.d, so no file of the person's is
//     edited). The block is found by its markers and replaced, so a second install writes it
//     once. The shell is the one $SHELL names unless given.
// `uninstall` removes the block and the link. Nothing else is touched.
static std::string self_path(const char* argv0) {
  char real[PATH_MAX];
#ifdef __APPLE__
  char buf[PATH_MAX];
  uint32_t n = sizeof buf;
  if (_NSGetExecutablePath(buf, &n) == 0) return realpath(buf, real) ? std::string(real) : std::string(buf);
#else
  char buf[PATH_MAX];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n > 0) { buf[n] = '\0'; return buf; }
#endif
  return realpath(argv0, real) ? std::string(real) : std::string(argv0);
}
static const char* kInstallBegin = "# >>> dirk (written by `dirktui install`; `dirktui uninstall` removes it) >>>";
static const char* kInstallEnd = "# <<< dirk <<<";
static std::string shell_of(int argc, char** argv) {
  if (argc >= 3) return argv[2];
  const char* sh = std::getenv("SHELL");
  if (!sh) return "";
  const std::string s(sh);
  return s.substr(s.rfind('/') + 1);
}
static bool mkdirs(const std::string& dir) {
  for (std::size_t i = 1; i <= dir.size(); ++i)
    if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
  struct stat st{};
  return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
// `text` with the marked block replaced by `block` (empty: removed), or appended.
static std::string with_block(const std::string& text, const std::string& block) {
  const std::size_t b = text.find(kInstallBegin);
  std::size_t e = b == std::string::npos ? std::string::npos : text.find(kInstallEnd, b);
  if (e != std::string::npos) {
    e += std::strlen(kInstallEnd);
    if (e < text.size() && text[e] == '\n') ++e;
    std::size_t bb = b;
    if (bb > 0 && text[bb - 1] == '\n' && block.empty()) --bb;  // take the blank line the block brought
    return text.substr(0, bb) + block + text.substr(e);
  }
  if (block.empty()) return text;
  std::string out = text;
  if (!out.empty() && out.back() != '\n') out += '\n';
  if (!out.empty()) out += '\n';
  return out + block;
}
int install_command(int argc, char** argv, bool remove) {
  const std::string shell = shell_of(argc, argv);
  const bool known = shell == "zsh" || shell == "bash" || shell == "fish";
  const char* home = std::getenv("HOME");
  if (!known || !home || !*home) {
    std::fprintf(stderr, "usage: dirktui %s zsh|bash|fish   (the shell defaults to $SHELL%s)\n",
                 remove ? "uninstall" : "install", home && *home ? "" : "; HOME is unset");
    return 2;
  }
  const std::string bin_dir = std::string(home) + "/.local/bin", link = bin_dir + "/dirktui";
  const std::string rc = shell == "zsh" ? std::string(home) + "/.zshrc"
                       : shell == "bash" ? std::string(home) + "/.bashrc"
                                         : std::string(home) + "/.config/fish/conf.d/dirk.fish";
  const std::string block =
      remove ? std::string()
      : shell == "fish"
          // GUARDED: a binary that is gone (the build tree moved) costs `dirk`, never a shell start.
          ? std::string(kInstallBegin) + "\nfish_add_path -g $HOME/.local/bin\ncommand -q dirktui; and dirktui init fish | source\n" + kInstallEnd + "\n"
          : std::string(kInstallBegin) + "\ncase \":$PATH:\" in *\":$HOME/.local/bin:\"*) ;; *) export PATH=\"$HOME/.local/bin:$PATH\" ;; esac\n"
                "command -v dirktui >/dev/null 2>&1 && eval \"$(dirktui init " + shell + ")\"\n" + kInstallEnd + "\n";
  // the link
  struct stat st{};
  const bool is_link = lstat(link.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
  const bool is_other = lstat(link.c_str(), &st) == 0 && !S_ISLNK(st.st_mode);
  if (remove) {
    if (is_link) { unlink(link.c_str()); std::fprintf(stderr, "dirktui: removed %s\n", link.c_str()); }
    else if (is_other) std::fprintf(stderr, "dirktui: %s is not a link this command made; left alone\n", link.c_str());
  } else {
    const std::string self = self_path(argv[0]);
    if (is_other) {
      std::fprintf(stderr, "dirktui: %s exists and is not a link; move it aside first\n", link.c_str());
      return 1;
    }
    if (!mkdirs(bin_dir)) { std::fprintf(stderr, "dirktui: cannot create %s\n", bin_dir.c_str()); return 1; }
    if (is_link) unlink(link.c_str());
    if (symlink(self.c_str(), link.c_str()) != 0) {
      std::fprintf(stderr, "dirktui: cannot link %s -> %s (%s)\n", link.c_str(), self.c_str(), std::strerror(errno));
      return 1;
    }
    std::fprintf(stderr, "dirktui: linked %s -> %s\n", link.c_str(), self.c_str());
  }
  // the rc file
  std::string text;
  {
    std::ifstream in(rc, std::ios::binary);
    if (in) text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  const std::string next = with_block(text, block);
  if (!remove && shell == "zsh" && text.find("zsh-autosuggestions") != std::string::npos)
    // The two share Right Arrow at the end of the line. The widget defers: a suggestion that is
    // showing is accepted first, and Right again opens dirk. Said once here, at the one moment a
    // person is reading; never at shell start.
    std::fprintf(stderr, "dirktui: note: zsh-autosuggestions is here too — Right Arrow accepts a suggestion that is showing; "
                         "with none showing, at the end of the line, it opens dirk (so: Right to accept, Right to browse)\n");
  if (remove && next == text) {
    std::fprintf(stderr, "dirktui: no dirk block in %s\n", rc.c_str());
  } else if (remove && shell == "fish" && next.empty()) {
    unlink(rc.c_str());
    std::fprintf(stderr, "dirktui: removed %s\n", rc.c_str());
  } else if (next != text) {
    if (!mkdirs(rc.substr(0, rc.rfind('/')))) { std::fprintf(stderr, "dirktui: cannot create the directory of %s\n", rc.c_str()); return 1; }
    std::ofstream out(rc, std::ios::binary | std::ios::trunc);
    out << next;
    if (!out) { std::fprintf(stderr, "dirktui: cannot write %s\n", rc.c_str()); return 1; }
    std::fprintf(stderr, "dirktui: %s the dirk block in %s\n", remove ? "removed" : (text.find(kInstallBegin) != std::string::npos ? "refreshed" : "wrote"), rc.c_str());
  } else {
    std::fprintf(stderr, "dirktui: %s already has the dirk block\n", rc.c_str());
  }
  if (!remove && shell == "bash") {
    // A LOGIN bash reads a profile and never ~/.bashrc unless the profile sources it — and a
    // terminal on macOS opens a login shell. Said, not done: the profile is the person's.
    const char* profiles[] = {"/.bash_profile", "/.bash_login", "/.profile"};
    std::string found, ptext;
    for (const char* pf : profiles) {
      std::ifstream in(std::string(home) + pf, std::ios::binary);
      if (in) { found = std::string(home) + pf; ptext.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); break; }
    }
    const bool sources = ptext.find(".bashrc") != std::string::npos;
#ifdef __APPLE__
    const bool login_shells = true;
#else
    const bool login_shells = false;
#endif
    if (!found.empty() && !sources)
      std::fprintf(stderr, "dirktui: note: a login bash reads %s, which does not source ~/.bashrc — add to it:  [ -f ~/.bashrc ] && . ~/.bashrc\n", found.c_str());
    else if (found.empty() && login_shells)
      std::fprintf(stderr, "dirktui: note: a terminal here opens a LOGIN bash, which reads ~/.bash_profile and not ~/.bashrc — create it with:  [ -f ~/.bashrc ] && . ~/.bashrc\n");
  }
  if (!remove) std::fprintf(stderr, "dirktui: open a new shell, or: %s\n", shell == "fish" ? "source ~/.config/fish/conf.d/dirk.fish" : ("source " + rc).c_str());
  return 0;
}

int init_command(int argc, char** argv) {
  const std::string shell = argc == 3 ? argv[2] : "";
  const char* script = shell == "zsh" ? kZshInit : shell == "bash" ? kBashInit : shell == "fish" ? kFishInit : nullptr;
  if (!script) {
    std::fprintf(stderr, "usage: dirktui init zsh|bash|fish\n");
    return 2;
  }
  std::fputs(script, stdout);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // A subcommand is the FIRST word and nothing else: a directory literally called `init` is
  // still reachable as `dirktui ./init`.
  if (argc >= 2 && std::string(argv[1]) == "init") return init_command(argc, argv);
  if (argc >= 2 && std::string(argv[1]) == "install") return install_command(argc, argv, false);
  if (argc >= 2 && std::string(argv[1]) == "uninstall") return install_command(argc, argv, true);
  std::string start;
  bool ambiguous = false;
  [[maybe_unused]] std::string presets_dir, layout_arg, theme_arg = "default-dark";
  [[maybe_unused]] std::string frame_spec, keys_spec;
  std::string apps_dirs = std::string("/Applications:") + (std::getenv("HOME") ? std::getenv("HOME") : "") + "/Applications";  // where application bundles live
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    [[maybe_unused]] auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    // WHAT A FLAG ON THIS COMMAND LINE MAY BE, and the four are not close:
    //   1. A SELF-TEST HOOK — compiled in only for `dirk-selftest`, which is this same
    //      source built again WITH them. The shipped binary does not contain them, so the binary
    //      that gets verified is not the one that ships.
    //   2. A REAL FEATURE RUN HEADLESSLY — a shipped capability reached without a terminal. Stays.
    //   3. A TERMINAL FACT — something true of the terminal the process cannot yet ask for.
    //      `--ambiguous-wide` is the last one; it becomes an auto-detected setting.
    //   4. CONFIGURATION — a theme, a layout, a bindings file, a preset directory, a mode, a
    //      depth. **These may never come back.** Each names something the preset system already
    //      holds, autosaves and offers a UI for, and a flag beside it is a second configuration
    //      system with neither discoverability nor persistence, competing with the one that has
    //      both — and winning by accident, because a flag is what a person finds first.
    // `rolltui-product-flags-test` holds all three products to this.
    if (a == "--ambiguous-wide") { ambiguous = true; continue; }  // a terminal fact, not a hook
#ifdef ROLLTUI_SELFTEST
    if (a == "--presets") presets_dir = next();
    else if (a == "--layout") layout_arg = next();
    else if (a == "--theme") theme_arg = next();
    else if (a == "--frame") frame_spec = next();
    else if (a == "--keys") keys_spec = next();
    else if (a == "--apps") apps_dirs = next();  // where application bundles are looked for, instead of /Applications
    else
#endif
    if (!a.empty() && a[0] != '-' && start.empty()) start = a;
    else return usage();
  }

  App app;
  app.ambiguous = ambiguous ? 1 : 0;
  {
    // The app's own MOTION file, through the same three rungs as its layout — embedded, beside
    // the binary, a person's config directory — and read BEFORE any theme, since every theme
    // load merges it. A missing file is a still app, not an error.
    RolltuiStr t{};
    if (rolltui_app_file(argv[0], "dirktui", "effects", dirktui_kAppFiles, dirktui_kAppFileCount, &t, nullptr))
      app.effects_json.assign(t.p ? t.p : "", t.n);
    rolltui_str_free(&t);
    RolltuiStr m{};
    if (rolltui_app_file(argv[0], "dirktui", "menu", dirktui_kAppFiles, dirktui_kAppFileCount, &m, nullptr))
      app.menu_json.assign(m.p ? m.p : "", m.n);
    rolltui_str_free(&m);
    app.load_settings(argv[0]);  // before the browser exists: it reads sort and dotfiles as it opens
    app.installed = detect_programs(apps_dirs);
  }
  app.set_theme(theme_arg.c_str());
  if (!app.effects) app.set_theme("default-dark");
  rolltui_context_set_dir(app.ctx, presets_dir.data(), presets_dir.size());

  // THE STORES, and the two lines that make the library's editors this app's. A kind is the
  // library's; what it edits is whatever store the host hands over, so an app with no store gets
  // an editor with nowhere to commit. Opened on the same directory the context resolves presets
  // through, so what the editors write is what the next start reads.
  {
    // The STORES read a person's own directory whether or not one was named on the command line —
    // an explicit `--presets` points the SCREEN somewhere, and where a person's presets live is a
    // separate question with its own answer.
    const std::string store_dir = presets_dir.empty() ? user_presets_dir() : presets_dir;
    RolltuiPresetDomain* td = rolltui_preset_domain_theme(app.ctx);
    RolltuiPresetDomain* bd = rolltui_preset_domain_bindings(app.ctx);
    RolltuiThemePresetReport trep{};
    RolltuiBindingsPresetReport brep{};
    app.theme_store = rolltui_preset_store_new(td, store_dir.data(), store_dir.size(), 0, "", 0);
    app.keys_store = rolltui_preset_store_new(bd, store_dir.data(), store_dir.size(), 0, "", 0);
    // A report is REQUIRED, not optional: a store that cannot say what it did on start would make
    // a missing preset directory look like a successful one.
    rolltui_preset_store_start(app.theme_store, &trep);
    rolltui_preset_store_start(app.keys_store, &brep);
    rolltui_theme_preset_report_release(&trep);
    rolltui_bindings_preset_report_release(&brep);
    rolltui_windows_set_theme_store(app.windows, "theme", 5, app.theme_store, /*persist=*/1);
    rolltui_windows_set_bindings_store(app.windows, "keys", 4, app.keys_store, /*persist=*/1);
    app.sync_theme();
  }

  {
    char cwd[4096];
    const std::string here = getcwd(cwd, sizeof cwd) ? cwd : ".";
    app.root = start.empty() ? here : (start[0] == '/' ? start : here + "/" + start);
    while (app.root.size() > 1 && app.root.back() == '/') app.root.pop_back();
    app.start_dir = app.root;  // what a relative path is said from, for the whole session
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
      const std::string name = layout_arg.empty() ? std::string("dirktui") : layout_arg;
      const bool path = name.find('/') != std::string::npos || name.find(".json") != std::string::npos;
      const std::string file = path ? name : presets_dir + "/layouts/" + name + ".json";
      bool ok = false;
      const std::string text = read_file(file, ok);
      if (ok) { loaded = load_layout_text(app.ctx, text, &rep); have = loaded != nullptr; }
      if (!ok) { rolltui_str_append(&layout_tried, "  missing ", 10);
                 rolltui_str_append(&layout_tried, file.data(), file.size()); }
    } else {
      RolltuiStr text{};
      if (rolltui_app_file(argv[0], "dirktui", "layout",
                           dirktui_kAppFiles, dirktui_kAppFileCount, &text, &layout_tried)) {
        loaded = load_layout_text(app.ctx, std::string(text.p ? text.p : "", text.n), &rep);
        have = loaded != nullptr;
      }
      rolltui_str_free(&text);
    }
  }
  if (!have) {
    // NAME WHAT WAS WANTED AND EVERY PLACE IT WAS SOUGHT. "no layout ()" was this message, and
    // an empty parenthesis is the standard this library enforces on everyone else, failed here.
    std::fprintf(stderr, "dirktui: cannot load its layout%s%s\n",
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
      ok = rolltui_app_file(argv[0], "dirktui", "bindings",
                            dirktui_kAppFiles, dirktui_kAppFileCount, &t, nullptr) != 0;
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
        std::fprintf(stderr, "dirktui: bindings/default.json: %s\n", why.c_str());
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
      std::fprintf(stderr, "dirktui: %s\n", say.c_str());
      rolltui_str_free(&say);
    }
    rolltui_gap_report_release(&gaps);
  }

#ifdef ROLLTUI_SELFTEST
  if (!frame_spec.empty()) {
    if (!parse_size(frame_spec, app.w, app.h)) return usage();
    App::headless = true;
    app.prepare();
    if (!keys_spec.empty()) {
      // THE CLOCK IS THE SCRIPT'S: every step carries the moment it happens at, and a `Tick`
      // step is a moment with no event. A script with no tick draws the STILL picture — the
      // clock is switched off for the frame, so a slide is at its end and every effect at its
      // first instant — and one with a tick draws that moment: `Right Tick:60` is the frame 60
      // ms into the slide the Right began.
      bool moving = false;
      for (const rolltui_selftest::Step& st : rolltui_selftest::scripted_keys(keys_spec, app.w, app.h)) {
        app.now_ms = st.ms;
        if (st.tick) { moving = true; continue; }
        app.handle(st.ev);
        // A FRAME BETWEEN EVENTS, as the live loop has: a popup opened by one key has its widget
        // built at the next sync, and the key after must find it there.
        app.prepare();
      }
      if (!moving) app.now_ms = 0;
      app.settle();
      // A script that accepts or cancels gets the PRODUCT's answer — the path or nothing, with
      // its exit status — and no frame, so the headless run and the real one leave the same bytes.
      if (app.quit) return app.finish();
      app.prepare();
    }
    RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    // What this frame's motion touched, for a test that cannot see a colour in a text frame.
    std::fprintf(stderr, "effects: marks=%zu drawn=%d cells=%d refused=%d\n", app.last_marks, app.last_fx.marks_drawn,
                 app.last_fx.cells_touched, app.last_fx.glyphs_refused);
    RolltuiStr text{};
    rolltui_frame_to_text(f, &text);
    std::fwrite(text.c_str(), 1, text.size(), stdout);
    rolltui_str_free(&text);
    rolltui_swap_free(swap);
    return 0;
  }
#endif

  // A PERSON AT A KEYBOARD IS THE PRECONDITION, checked on stdin BEFORE the terminal is touched.
  // dirk takes its keys from whoever is typing, and a stdin that is a pipe means there is nobody —
  // a script, a test harness, `yes | dirk`. Refusing here is what keeps a headless run from
  // opening the controlling terminal and sitting in raw mode waiting for a key nobody will press.
  // The shell side redirects `< /dev/tty` for the same reason fzf's widget does.
  if (!isatty(STDIN_FILENO)) {
    std::fprintf(stderr, "dirktui: stdin is not a terminal, and dirk takes its keys from the keyboard\n");
    return 2;
  }
  // THE SCREEN IS /dev/tty, ALWAYS — not stdout when stdout happens to be a terminal. One
  // behaviour, whether run bare or inside `$(dirk)`, and stdout carries nothing but the answer.
  // No /dev/tty means nowhere to draw: said by name, exit 2, never a hang reading a pipe.
  const int tty = open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (tty < 0) {
    std::fprintf(stderr, "dirktui: cannot open /dev/tty (%s): no terminal to draw on\n", std::strerror(errno));
    return 2;
  }
  RolltuiTerminalOptions opts{};
  RolltuiTerminal* term = rolltui_terminal_new(tty, tty, opts);
  if (!rolltui_terminal_is_tty(term)) {
    rolltui_terminal_free(term);
    close(tty);
    std::fprintf(stderr, "dirktui: /dev/tty is not a terminal\n");
    return 2;
  }
  app.w = rolltui_terminal_width(term);
  app.h = rolltui_terminal_height(term);
  RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
  app.term = term;   // for a program that takes the terminal over
  app.swap = swap;
  app.tty_fd = tty;
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
    app.now_ms = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    const int timeout = app.poll_timeout_ms(f, 250);
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
        term, timeout,
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
    app.settle();
    if (pending.resized) { app.w = pending.w; app.h = pending.h; }
  }
  rolltui_str_free(&out);
  rolltui_swap_free(swap);
  rolltui_terminal_free(term);  // restores the screen BEFORE the answer is written
  close(tty);
  return app.finish();
}
