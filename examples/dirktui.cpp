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
#include <cctype>
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
enum class Show { WithSort, Always, Never };  // a size or a modified column: with the sort, always, never
enum class Exec { Line, Run, Open };  // Enter on an executable: to the command line, run here, the system opener

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
  const char* exe;     // a command on PATH, or NULL
  const char* bundle;  // an application bundle's name under the applications directories, or NULL
  const char* arg;     // one fixed argument before the file, or NULL
  bool terminal;       // takes the terminal over: dirktui steps aside and comes back when it exits
};
// EVERY KNOWN PROGRAM IS LISTED, installed or not: the menu shows the ones that are not as
// disabled, so a person sees what COULD open a type and what is missing. Detection: the command
// on PATH, or the bundle in an applications directory; a program found by its bundle only is
// run through `open -a`.
static const Program kPrograms[] = {
    {"nvim", "Neovim", "nvim", nullptr, nullptr, true},
    {"vim", "Vim", "vim", nullptr, nullptr, true},
    {"hx", "Helix", "hx", nullptr, nullptr, true},
    {"micro", "micro", "micro", nullptr, nullptr, true},
    {"nano", "nano", "nano", nullptr, nullptr, true},
    {"emacs", "Emacs, in the terminal", "emacs", nullptr, "-nw", true},
    {"less", "less", "less", nullptr, nullptr, true},
    {"bat", "bat", "bat", nullptr, "--paging=always", true},
    {"code", "Visual Studio Code", "code", "Visual Studio Code", nullptr, false},
    {"zed", "Zed", "zed", "Zed", nullptr, false},
    {"subl", "Sublime Text", "subl", "Sublime Text", nullptr, false},
    {"idea", "IntelliJ IDEA", "idea", "IntelliJ IDEA", nullptr, false},
    {"pycharm", "PyCharm", "pycharm", "PyCharm", nullptr, false},
    {"xcode", "Xcode", nullptr, "Xcode", nullptr, false},
    {"studio", "Android Studio", "studio", "Android Studio", nullptr, false},
    {"notes", "Notes", nullptr, "Notes", nullptr, false},
    {"bbedit", "BBEdit", "bbedit", "BBEdit", nullptr, false},
    {"mate", "TextMate", "mate", "TextMate", nullptr, false},
    {"nova", "Nova", "nova", "Nova", nullptr, false},
    {"textedit", "TextEdit", nullptr, "TextEdit", nullptr, false},
    {"preview", "Preview", nullptr, "Preview", nullptr, false},
    {"safari", "Safari", nullptr, "Safari", nullptr, false},
    {"chrome", "Google Chrome", nullptr, "Google Chrome", nullptr, false},
    {"firefox", "Firefox", nullptr, "Firefox", nullptr, false},
};
static constexpr const char* kSystemProgram = "system";  // the platform's opener: `open`, `xdg-open`
static constexpr const char* kShellProgram = "shell";    // not opened: handed to the command line
static constexpr const char* kRunProgram = "run";        // an executable, run here in the terminal
struct TypeGroup {
  const char* id;
  const char* exts;    // space-separated, lower-case
  const char* prefer;  // program ids in the order the group's DEFAULT is picked from what is installed
};
#define EDITORS "nvim hx micro code zed subl bbedit nova mate vim nano emacs xcode textedit notes less bat"
#define IDE_FIRST "code zed subl idea nvim hx micro bbedit nova mate vim nano emacs xcode textedit"
static const TypeGroup kGroups[] = {
    {"text", "txt md markdown rst log tex", EDITORS},
    {"data", "json yaml yml toml csv tsv xml ini cfg conf plist", EDITORS},
    {"c", "c h cc cpp cxx hpp hh m mm", "xcode code zed subl nvim hx micro bbedit nova mate vim nano emacs"},
    {"java", "java kt kts scala", "idea studio code zed subl nvim hx micro vim nano emacs"},
    {"python", "py pyi", "pycharm code zed subl nvim hx micro vim nano emacs"},
    {"js", "js ts jsx tsx mjs cjs", IDE_FIRST},
    {"shell", "sh bash zsh fish", "nvim hx micro code zed subl vim nano emacs"},
    {"rust_go", "rs go", IDE_FIRST},
    {"swift", "swift", "xcode code zed subl nvim hx vim nano emacs"},
    {"scripts", "rb php pl lua", IDE_FIRST},
    {"build", "cmake mk make ninja", EDITORS},
    {"web", "html htm svg css", "system safari chrome firefox code zed subl nvim hx vim"},
    {"images", "png jpg jpeg gif webp bmp tiff heic", "system preview"},
    {"docs", "pdf doc docx xls xlsx ppt pptx pages numbers key epub rtf", "system preview"},
    {"media", "mp3 wav m4a flac mp4 mov m4v", "system"},
    {"apps", "app", "system"},  // a bundle: opened as the application it is, or handed to the command line
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
// Which known programs are installed, and how: `on_path` for a command, `bundled` for an
// application bundle under one of `apps_dirs` (colon-separated). Read once; the answer is the
// menu's contents.
struct Installed {
  std::set<std::string> on_path, bundled;
  bool has(const std::string& id) const { return on_path.count(id) || bundled.count(id); }
};
static Installed detect_programs(const std::string& apps_dirs) {
  Installed out;
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
    if (p.exe)
      for (const std::string& d : path)
        if (access((d + "/" + p.exe).c_str(), X_OK) == 0) { out.on_path.insert(p.id); break; }
    if (p.bundle)
      for (const std::string& d : apps) {
        struct stat st{};
        if (stat((d + "/" + p.bundle + ".app").c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { out.bundled.insert(p.id); break; }
      }
  }
  return out;
}

struct Options {
  bool hidden = true;  // dotfiles shown unless a person turns them off
  Sort sort = Sort::Name;
  bool reversed = false;  // the sort's order turned around: z to a, smallest first, oldest first
  bool motion = true;    // the column slide; off snaps
  bool sparkle = true;   // the cursor's glow, the trail's sparkle, the opened burst; off is a still app
  bool dividers = true;  // a hairline in the margin between columns, in the border colour
  Show show_size = Show::WithSort;      // a size column after the name
  Show show_modified = Show::WithSort;  // a modified column likewise
  // WHAT ENTER ON A FILE DOES. A folder is always entered. A file is a leaf: an EXECUTABLE goes
  // to the command line typed out, so arguments can follow (a picker never runs anything); a
  // document opens with the program chosen for its TYPE (`open_with`, from what is installed, see
  // `kGroups`); a type nobody listed goes to the command line too. `leave` overrides all of it:
  // every file goes to the command line, and the cursor leaves with it.
  bool leave = false;
  Exec exec = Exec::Line;  // an executable or a script with the x bit: the command line (arguments can follow), run here, or the opener
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
// the ends instead of wrapping; its brightness falls off as a bell, `width` cells wide. THE
// TURNS ARE A BAND'S WIDTH PAST EACH END, so every cell — of a one-letter name as much as a long
// one — sees the whole rise and fall; a band that turned at the last cell left a short name lit
// at the crest the whole time and never dim.
void fx_glow(void*, const RolltuiEffectSpec* s, const RolltuiStyle* styles, const void*, const RolltuiEffectCell* in,
             RolltuiEffectOut* out) {
  const int period = s->period_ms > 0 ? s->period_ms : 2000;
  const double width = s->width > 0 ? s->width : 3.0;
  const double u = static_cast<double>(in->elapsed_ms % static_cast<unsigned long long>(period)) / period;
  const double tri = u < 0.5 ? u * 2.0 : (1.0 - u) * 2.0;
  const double centre = -width + tri * ((in->length > 1 ? in->length - 1 : 0) + 2.0 * width);
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
  std::string hint;   // this app's own last word (a bad path, a copy): shown, held, then faded out
  std::string hint_shown;              // what the line last showed, to notice a new one
  unsigned long long hint_since = 0;   // when it appeared, on the frame clock
  static constexpr unsigned long long kHintHoldMs = 2000, kHintFadeMs = 600, kTitleBackMs = 300;
  unsigned long long title_back_since = 0;  // when the last note went, so the name can fade back in
  // CALLER-FILLED, one per run: the status line's fields, reset and refilled every frame so
  // the array and each row's buffer are reused rather than rebuilt.
  RolltuiRows status_rows{};
  // THE KEYS ON THE STATUS LINE ARE A HINT BAR — the library's — built from the live bindings
  // and rebuilt when they change; a press on a hint runs its action as the key would.
  RolltuiHintBar* hints = rolltui_hint_bar_new();  // OWNED
  // THE PATH LINE, WHILE NOT BEING EDITED, IS A BREADCRUMB — the library's hint bar with " › "
  // between its parts and its tail kept: each part is that column, clicked; the pencil at the
  // end, always in view, opens the line for editing with the whole path selected.
  RolltuiHintBar* crumbs = rolltui_hint_bar_new();  // OWNED
  std::string crumbs_for;  // the path the crumbs were last built from
  int crumbs_used = 0;     // how many cells the bar took on its last draw: right of that is the pencil's
  bool hints_built = false;
  RolltuiRows status_facts{};  // the fields after the bar, reset and refilled every frame
  // Each part of the path is a crumb whose action names its column — "/" is column 0, the first
  // component column 1 — and the pencil's action is "edit".
  // Each part of the path is a crumb, and a crumb is an ENTRY: part k is the entry selected in
  // column k — "/" in the top column, which holds only it — so a click on it puts the cursor
  // there: that column focused with the part highlighted, its listing kept as the preview, the
  // deeper columns gone, and the breadcrumb shrinks to the part clicked. The last part is the
  // entry under the cursor: its action is "here" — a folder is entered by it, a file is where
  // the cursor already is.
  void build_crumbs(const std::string& path) {
    crumbs_for = path;
    rolltui_hint_bar_clear(crumbs);
    rolltui_hint_bar_add(crumbs, "", 0, "/", 1, "crumb:0", 7);
    const std::string dir = current_dir();
    const bool has_sel = path != dir && path.rfind(dir == "/" ? "/" : dir + "/", 0) == 0;
    std::size_t at = 1, col = 1;
    while (at < path.size()) {
      const std::size_t slash = path.find('/', at);
      const std::string part = path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
      const bool last = slash == std::string::npos;
      const std::string action = has_sel && last ? std::string("here") : "crumb:" + std::to_string(col++);
      if (!part.empty()) rolltui_hint_bar_add(crumbs, "", 0, part.data(), part.size(), action.data(), action.size());
      if (last) break;
      at = slash + 1;
    }
    const char* pencil = ambiguous ? "edit" : "\xE2\x9C\x8E";  // ✎, or the word where its width is not one cell
    rolltui_hint_bar_add(crumbs, "", 0, pencil, std::strlen(pencil), "edit", 4);
  }
  bool editing_path() const { return focused_content() == "input:path"; }
  // Where the path line is this frame — the layout's, so a screen without one has no breadcrumb.
  bool path_rect(RolltuiRect& r) const { return rolltui_windows_window_rect(windows, "path", 4, &r) != 0 && r.w > 0 && r.h > 0; }
  // A press on the path line while it is a breadcrumb: a part focuses its column, the pencil
  // opens the line for editing with the whole path selected; the rest of the row is nobody's.
  bool press_on_crumbs(int x, int y) {
    std::size_t n = 0;
    RolltuiRect r{};
    if (!path_rect(r)) return false;
    const char* a = rolltui_hint_bar_hit(crumbs, x, y, &n);
    if (!a && y >= r.y && y < r.y + r.h && x >= r.x + crumbs_used) { a = "edit"; n = 4; }  // right of the pencil is the pencil's
    if (!a) return y >= r.y && y < r.y + r.h;  // the row is the crumbs': a press beside them does nothing
    const std::string action(a, n);
    if (action == "edit") {
      rolltui_window_stack_focus(stack, "path", 4);
      if (RolltuiInput* in = rolltui_windows_input(windows, "path", 4)) rolltui_input_select_all(in);
    } else if (action.rfind("crumb:", 0) == 0) {
      // Part k is the entry selected in column k: "/" in the top column, the first component in
      // the root's listing, and so on.
      rolltui_windows_picker_focus_column(windows, kPicker, 10, static_cast<std::size_t>(std::atoi(action.c_str() + 6)));
    } else if (action == "here") {
      RolltuiStr sel{};
      int is_dir = 0;
      if (rolltui_windows_picker_selected(windows, kPicker, 10, &sel, &is_dir) && sel.n && is_dir) jump(std::string(sel.p, sel.n));
      rolltui_str_free(&sel);
    }
    return true;
  }
  void build_keys_hint() {
    struct Row { const char* action; const char* what; };
    // The sort and the dotfiles are STATES as well as keys: their labels say the state, and the
    // bar is rebuilt when either changes.
    const std::string sort = std::string("sort: ") + sort_words(opt.sort, opt.reversed);
    const std::string dots = opt.hidden ? "+dotfiles" : "\xE2\x88\x92" "dotfiles";
    const Row rows[] = {{"app.help", "help"}, {"app.menu", "settings"}, {"picker.copy", "copy"}, {"app.sort", sort.c_str()}, {"app.hidden", dots.c_str()}, {"app.details", "details"}};
    rolltui_hint_bar_clear(hints);
    for (const Row& r : rows) {
      const std::size_t n = rolltui_bindings_chord_count(bindings, r.action, std::strlen(r.action));
      if (n == 0) continue;
      // THE FIRST CHORD THIS TERMINAL CAN DELIVER, not the first in the file: `ctrl+.` needs a key
      // protocol, and where there is none the hint says `Alt-H`, which arrives anywhere.
      RolltuiChord c{};
      std::size_t pick = 0;
      for (std::size_t i = 0; i < n; ++i) {
        RolltuiChord k{};
        rolltui_bindings_chord_at(bindings, r.action, std::strlen(r.action), i, &k);
        if (rolltui_key_deliverable(&k, rolltui_key_active_protocol())) { pick = i; break; }
      }
      rolltui_bindings_chord_at(bindings, r.action, std::strlen(r.action), pick, &c);
      char buf[ROLLTUI_CHORD_STRING_MAX];
      const std::size_t bn = rolltui_chord_display(&c, buf, sizeof buf);
      rolltui_hint_bar_add(hints, buf, bn, r.what, std::strlen(r.what), r.action, std::strlen(r.action));
    }
    hints_built = true;
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
    // A note holds, then fades, then the name fades back in: frames until all of that is done.
    if (now_ms && (!hint.empty() || now_ms < title_back_since + kTitleBackMs) && want > 100) want = 100;
    if (opt.sparkle && f && rolltui_frame_mark_count(f) != 0 && effects && !rolltui_effect_map_empty(effects)) {
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
  std::string matches_json;  // the find dialog's list, a file like the menu — empty until a find
  std::string path_shown;    // what the path line was last set to: the folder the cursor is in
  std::string focused_before; // the focused window's content before an event, for a focus change
  // A FIND: the query, and what it matched under the folder the cursor was in.
  struct Match { std::string path, shown; int score; bool folder; };
  std::vector<Match> found;
  std::string find_query, find_under;
  bool matches_dirty = false;  // the dialog was just opened: its list is filled at the next sync
  std::size_t last_entries = 0, last_column = 0, last_columns = 0;  // the picker's counts, for the headless report
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
    rolltui_hint_bar_free(hints);
    rolltui_hint_bar_free(crumbs);
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
    o.reversed = opt.reversed ? 1 : 0;
    o.motion = opt.motion ? 1 : 0;
    o.dividers = opt.dividers ? 1 : 0;
    o.show_size = show_code(opt.show_size);
    o.show_modified = show_code(opt.show_modified);
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
  // The sort said in words a status line shares: the field and which way it runs, always
  // the same width (a 4-letter field, a space, one arrow) so the hint bar does not resize
  // as the sort cycles. `<` is ascending (a-z, smallest first, oldest first), `>` descending
  // — one meaning for the arrow across all three fields, unlike `rev` itself, whose default
  // (unreversed) direction is ascending for name but descending for size and modified.
  static std::string sort_words(Sort s, bool rev) {
    const char* field = s == Sort::Name ? "name" : s == Sort::Size ? "size" : "date";
    const bool ascending = s == Sort::Name ? !rev : rev;
    return std::string(field) + (ascending ? " <" : " >");
  }
  // The menu's option id for a sort — `name`, `name_rev`, … — and back.
  static std::string sort_id(Sort s, bool rev) { return std::string(sort_name(s)) + (rev ? "_rev" : ""); }
  static bool sort_from_id(const std::string& id, Sort& s, bool& rev) {
    const bool r = id.size() > 4 && id.compare(id.size() - 4, 4, "_rev") == 0;
    const std::string key = r ? id.substr(0, id.size() - 4) : id;
    if (key != "name" && key != "size" && key != "modified") return false;
    s = key == "size" ? Sort::Size : key == "modified" ? Sort::Modified : Sort::Name;
    rev = r;
    return true;
  }
  static const char* exec_name(Exec e) { return e == Exec::Run ? "run" : e == Exec::Open ? "open" : "line"; }
  static Exec exec_from(const std::string& v) { return v == "run" ? Exec::Run : v == "open" ? Exec::Open : Exec::Line; }
  static unsigned char show_code(Show s) { return s == Show::Always ? ROLLTUI_SHOW_ALWAYS : s == Show::Never ? ROLLTUI_SHOW_NEVER : ROLLTUI_SHOW_WITH_SORT; }
  static const char* show_name(Show s) { return s == Show::Always ? "always" : s == Show::Never ? "never" : "sort"; }
  static Show show_from(const std::string& v) { return v == "always" ? Show::Always : v == "never" ? Show::Never : Show::WithSort; }
  // A settings file from before the three-way choice said true or false: true was "always",
  // and false was "only with the sort", which is what WithSort says.
  static Show show_from_json(const RolltuiJsonValue* v) {
    if (!v) return Show::WithSort;
    if (rolltui_json_is_bool(v)) return rolltui_json_as_bool(v, 0) ? Show::Always : Show::WithSort;
    std::size_t n = 0;
    const char* s = rolltui_json_as_string(v, "sort", 4, &n);
    return show_from(std::string(s, n));
  }
  void load_settings(const char* argv0) {
    RolltuiStr t{};
    if (rolltui_app_file(argv0, "dirktui", "settings", nullptr, 0, &t, nullptr)) {
      RolltuiStr err{};
      if (RolltuiJsonValue* root = rolltui_json_parse(t.p ? t.p : "", t.n, &err)) {
        opt.motion = rolltui_json_as_bool(rolltui_json_get(root, "motion", 6), 1) != 0;
        opt.sparkle = rolltui_json_as_bool(rolltui_json_get(root, "sparkle", 7), 1) != 0;
        opt.dividers = rolltui_json_as_bool(rolltui_json_get(root, "dividers", 8), 1) != 0;
        opt.show_size = show_from_json(rolltui_json_get(root, "show_size", 9));
        opt.show_modified = show_from_json(rolltui_json_get(root, "show_modified", 13));
        opt.reversed = rolltui_json_as_bool(rolltui_json_get(root, "reversed", 8), 0) != 0;
        opt.hidden = rolltui_json_as_bool(rolltui_json_get(root, "hidden", 6), 1) != 0;
        std::size_t n = 0;
        const char* sv = rolltui_json_as_string(rolltui_json_get(root, "sort", 4), "name", 4, &n);
        const std::string sort(sv, n);
        opt.sort = sort == "size" ? Sort::Size : sort == "modified" ? Sort::Modified : Sort::Name;
        opt.leave = rolltui_json_as_bool(rolltui_json_get(root, "leave", 5), 0) != 0;
        { const char* xv = rolltui_json_as_string(rolltui_json_get(root, "exec", 4), "line", 4, &n); opt.exec = exec_from(std::string(xv, n)); }
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
    out << "{ \"motion\": " << (opt.motion ? "true" : "false") << ", \"sparkle\": " << (opt.sparkle ? "true" : "false") << ", \"dividers\": " << (opt.dividers ? "true" : "false")
        << ", \"show_size\": \"" << show_name(opt.show_size) << "\", \"show_modified\": \"" << show_name(opt.show_modified) << "\""
        << ", \"hidden\": " << (opt.hidden ? "true" : "false")
        << ", \"sort\": \"" << sort_name(opt.sort) << "\", \"reversed\": " << (opt.reversed ? "true" : "false") << ", \"leave\": " << (opt.leave ? "true" : "false")
        << ", \"exec\": \"" << exec_name(opt.exec) << "\""
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
    { const std::string sid = sort_id(opt.sort, opt.reversed); rolltui_menu_set_value(m, "sort", 4, sid.data(), sid.size()); }
    rolltui_menu_set_checked(m, "hidden", 6, opt.hidden ? 1 : 0);
    rolltui_menu_set_checked(m, "motion", 6, opt.motion ? 1 : 0);
    rolltui_menu_set_checked(m, "sparkle", 7, opt.sparkle ? 1 : 0);
    rolltui_menu_set_checked(m, "dividers", 8, opt.dividers ? 1 : 0);
    rolltui_menu_set_value(m, "show_size", 9, show_name(opt.show_size), std::strlen(show_name(opt.show_size)));
    rolltui_menu_set_value(m, "show_modified", 13, show_name(opt.show_modified), std::strlen(show_name(opt.show_modified)));
    rolltui_menu_set_checked(m, "leave", 5, opt.leave ? 1 : 0);
    rolltui_menu_set_value(m, "exec", 4, exec_name(opt.exec), std::strlen(exec_name(opt.exec)));
    rolltui_menu_set_checked(m, "land", 4, opt.land_in_file_folder ? 1 : 0);
    rolltui_menu_set_checked(m, "relative", 8, opt.copy_relative ? 1 : 0);
    // THE THEME AND THE KEY BINDINGS are the stores' presets, listed live — a preset saved a
    // moment ago in the editor is in the list — with the current one as the value.
    fill_store_choice(m, "theme", 5, theme_store, "default-dark");
    fill_store_choice(m, "keys", 4, keys_store, "default");
    // THE "OPEN WITH" CHOICES ARE FILLED HERE, not in the file: their options are what this
    // machine has. The skeleton (one choice per type group) is the file's; the contents are
    // what `detect_programs` found, the same move roll makes with its preset listings.
    for (const TypeGroup& g : kGroups) {
      const std::string id = std::string("open_") + g.id;
      RolltuiMenuItemList options{};
      const std::string preset = default_program_for(g);  // what an unset group opens with: said on its option
      for (const Offer& of : options_for(g)) {
        RolltuiMenuItem* o = rolltui_menu_list_add(&options);
        const std::string label = of.id == preset ? of.label + " (default)" : of.label;
        rolltui_menu_item_set(o, ROLLTUI_MENU_ACTION, of.id.c_str(), of.id.size(), label.c_str(), label.size(), nullptr, 0);
        o->enabled = of.enabled ? 1 : 0;
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
  Installed installed;  // what is on this machine, and how it was found
  // What the menu offers for a group: EVERY program in the group's own order, the ones not
  // installed disabled, then the system opener and the command line. What is chosen is the
  // setting when it is installed, and the first installed preference otherwise.
  struct Offer { std::string id, label; bool enabled; };
  std::vector<Offer> options_for(const TypeGroup& g) const {
    std::vector<Offer> out;
    std::istringstream in(g.prefer);
    std::string id;
    while (in >> id) {
      if (id == kSystemProgram) continue;
      if (const Program* p = program_named(id)) out.push_back({id, p->label, installed.has(id)});
    }
    out.push_back({kSystemProgram, "the system opener", true});
    out.push_back({kShellProgram, "the command line (not opened)", true});
    return out;
  }
  // What a group opens with when nothing is set: the first of its preferences that is installed.
  std::string default_program_for(const TypeGroup& g) const {
    std::istringstream in(g.prefer);
    std::string id;
    while (in >> id) if (id == kSystemProgram || installed.has(id)) return id;
    return kSystemProgram;
  }
  std::string program_for(const TypeGroup& g) const {
    if (const auto it = opt.open_with.find(g.id); it != opt.open_with.end()) {
      const std::string& want = it->second;
      if (want == kSystemProgram || want == kShellProgram || installed.has(want)) return want;
    }
    return default_program_for(g);
  }
  // The store's presets as the options, the one this app starts with marked "(default)".
  static void fill_store_choice(RolltuiMenu* m, const char* id, std::size_t id_len, const RolltuiPresetStore* store, const char* preset) {
    if (!store) return;
    RolltuiMenuItemList options{};
    RolltuiPresetList presets{};
    rolltui_preset_store_list(store, &presets);
    for (std::size_t i = 0; i < presets.n; ++i) {
      const RolltuiPresetInfo& p = presets.v[i];
      RolltuiMenuItem* o = rolltui_menu_list_add(&options);
      const std::string name(p.name.p ? p.name.p : "", p.name.n);
      const std::string label = name == preset ? name + " (default)" : name;
      rolltui_menu_item_set(o, ROLLTUI_MENU_ACTION, p.name.p, p.name.n, label.data(), label.size(), p.shipped ? nullptr : "yours", p.shipped ? 0 : 5);
    }
    rolltui_preset_list_release(&presets);
    rolltui_menu_set_options(m, id, id_len, &options);
    rolltui_menu_list_release(&options);
    RolltuiStr label{};
    rolltui_preset_store_label(store, &label);  // "name", or "name (modified)": the name is the value
    std::string cur(label.p ? label.p : "", label.n);
    if (const std::size_t sp = cur.find(" ("); sp != std::string::npos) cur.erase(sp);
    rolltui_menu_set_value(m, id, id_len, cur.data(), cur.size());
    rolltui_str_free(&label);
  }
  // A CHOSEN KEY-BINDINGS PRESET becomes the live table the way the start built it: the store's
  // working copy, then this app's own bindings file on top, then the layout's declarations.
  std::string bindings_json;  // the app's bindings file, kept for that rebuild
  // Judged against the key protocol IN FORCE — the one the terminal negotiated, legacy until
  // there is a terminal — and the chords it cannot deliver come back as text, for the status line.
  std::string rebuild_bindings() {
    std::string undeliverable;
    if (!keys_store) return undeliverable;
    RolltuiBindings* w = static_cast<RolltuiBindings*>(rolltui_preset_store_working(keys_store));
    if (!w) return undeliverable;
    rolltui_bindings_free(bindings);
    bindings = rolltui_bindings_clone(w);
    rolltui_preset_store_value_free(keys_store, w);
    if (!bindings_json.empty()) {
      RolltuiBindingsReport brep{};
      rolltui_bindings_load_json(bindings, bindings_json.data(), bindings_json.size(), rolltui_key_active_protocol(),
                                 rolltui_bindings_library_scope, nullptr, nullptr, nullptr, &brep);
      for (std::size_t i = 0; i < brep.undeliverable_n; ++i) {
        if (!undeliverable.empty()) undeliverable += "; ";
        undeliverable.append(brep.undeliverable[i].p ? brep.undeliverable[i].p : "", brep.undeliverable[i].n);
      }
      rolltui_bindings_report_release(&brep);
    }
    std::size_t an = 0;
    const RolltuiLayoutAction* av = rolltui_layout_actions(layout, &an);
    rolltui_bindings_declare(bindings, av, an, nullptr, 0);
    rolltui_context_set_bindings(ctx, bindings);
    hints_built = false;
    return undeliverable;
  }
  // ONCE THE TERMINAL HAS SAID WHAT IT SPEAKS, the chords are judged again: a chord the file
  // names that cannot arrive here is a note on the status line — with the one that can shown on
  // the bar — and never a line printed before the terminal was asked.
  void terminal_ready() {
    const std::string cannot = rebuild_bindings();
    // NOTHING TO SAY WHILE EVERYTHING WORKS: a chord that cannot arrive is only worth a word when
    // its action has no other chord that can — `ctrl+.` beside `alt+h` is not a problem, it is a
    // second spelling. The report names chords; the question is about actions.
    std::string stranded;
    std::size_t at = 0;
    while (at < cannot.size()) {
      std::size_t end = cannot.find("; ", at);
      if (end == std::string::npos) end = cannot.size();
      const std::string entry = cannot.substr(at, end - at);
      const std::string action = entry.substr(0, entry.find(": '"));
      bool any = false;
      const std::size_t n = rolltui_bindings_chord_count(bindings, action.data(), action.size());
      for (std::size_t i = 0; i < n && !any; ++i) {
        RolltuiChord k{};
        rolltui_bindings_chord_at(bindings, action.data(), action.size(), i, &k);
        any = rolltui_key_deliverable(&k, rolltui_key_active_protocol()) != 0;
      }
      if (!any) { if (!stranded.empty()) stranded += "; "; stranded += entry; }
      at = end + 2;
    }
    if (!stranded.empty()) hint = "no key for this on this terminal: " + stranded;
  }
  static std::string program_label(const std::string& id) {
    if (const Program* p = program_named(id)) return p->label;
    return id == kSystemProgram ? "the system opener" : id;
  }

  void mount() {
    if (!menu_json.empty()) rolltui_context_add_menu(ctx, "places", 6, menu_json.data(), menu_json.size());
    if (!matches_json.empty()) rolltui_context_add_menu(ctx, "matches", 7, matches_json.data(), matches_json.size());
    rolltui_windows_bind_rows(windows, "entry", 5, entry_rows, this, nullptr);
    // THE PATH LINE keeps its text on Enter (it IS the path); THE FIND FIELD keeps its query so a
    // cancelled dialog leaves it there to refine.
    rolltui_windows_bind_submit(windows, "path", 4, on_submit, this, nullptr, /*on_submit=*/1);
    rolltui_windows_bind_submit(windows, "find", 4, on_find, this, nullptr, /*on_submit=*/1);
    rolltui_hint_bar_set_separator(crumbs, " \xE2\x80\xBA ", 5);  // " › "
    rolltui_hint_bar_set_keep_tail(crumbs, 1);
    // COPY IN THE PATH LINE goes where the picker's copy goes: the clipboard.
    if (RolltuiInput* in = rolltui_windows_input(windows, "path", 4))
      rolltui_input_set_copy(in, [](void* ctx, const char* t, std::size_t n) { App& a = *static_cast<App*>(ctx); a.hint = copy_to_clipboard(std::string(t, n)) ? "copied" : "could not copy: no clipboard command"; }, this);
    // THE PATH LINE IS A PATH, NOT A PROMPT: nothing before it. THE FIND LINE says what it is,
    // and what to type, as a placeholder — one row, clipped, never a note that takes a second.
    set_prompt("path", 4, "", 0, "", 0);
    set_prompt("find", 4, "find: ", 6, "a name, or part of one, under this folder; Enter lists what matches", 67);
    rolltui_context_set_help(ctx, "", 0, "", 0);
    rolltui_context_clear_help_scopes(ctx);
    for (const std::string& s : help_scopes()) rolltui_context_add_help_scope(ctx, s.data(), s.size());
    rolltui_window_stack_set_base(stack, rolltui_layout_base(layout));
    rolltui_window_stack_set_level_fn(stack, rolltui_windows_back, windows);  // a close closes one level
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
  // THE PATH LINE, submitted: go there and hand the focus back to the columns. A bad path is a
  // NAMED problem on the status line and the text stays for correcting.
  static void on_submit(void* ctx, const char* text, std::size_t len) {
    App& a = *static_cast<App*>(ctx);
    std::string path(text, len);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    if (path.empty()) { a.hint = "a path, please"; return; }
    if (path[0] == '~') { const char* home = std::getenv("HOME"); path = (home ? home : "") + path.substr(1); }
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) { a.hint = "no such path: " + path; return; }
    a.go_to_match(path);  // a folder is entered, a file selected in its folder; the focus goes back to the columns
  }
  static void on_find(void* ctx, const char* text, std::size_t len) {
    App& a = *static_cast<App*>(ctx);
    a.find(std::string(text, len));
  }

  void set_prompt(const char* source, std::size_t len, const char* prompt, std::size_t plen, const char* holder, std::size_t hlen) {
    RolltuiInput* in = rolltui_windows_input(windows, source, len);
    if (!in) return;
    RolltuiInputOptions o{};
    rolltui_input_options_copy(&o, rolltui_input_options(in));
    rolltui_str_set(&o.prompt, prompt, plen);
    rolltui_str_set(&o.placeholder, holder, hlen);
    o.single_line = 1;  // ONE ROW, sliding under the caret: a long path never takes a second
    rolltui_input_set_options(in, &o);
    rolltui_input_options_release(&o);
  }
  bool jump(std::string path) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    if (path.empty()) { hint = "a path, please"; return false; }
    if (path[0] == '~') { const char* home = std::getenv("HOME"); path = (home ? home : "") + path.substr(1); }
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) { hint = "no such path: " + path; return false; }
    if (!S_ISDIR(st.st_mode)) { hint = "not a directory: " + path; return false; }
    root = path;
    picker_go(path);
    hint.clear();  // the path line says where
    return true;
  }
  // The layout's own first focus — the columns — without this file naming their id.
  void focus_base() {
    std::size_t n = 0;
    const char* id = rolltui_layer_focus(rolltui_layout_base(layout), &n);
    if (n) rolltui_window_stack_focus(stack, id, n);
  }
  std::string focused_content() const {
    const RolltuiLayoutNode* n = rolltui_window_stack_focused(stack);
    if (!n) return std::string();
    std::size_t idn = 0, cn = 0;
    const char* id = rolltui_layout_node_id(n, &idn);
    const char* c = rolltui_windows_content_at(windows, id, idn, &cn);
    return c ? std::string(c, cn) : std::string();
  }
  // WHAT THE PATH LINE SHOWS: the entry under the cursor — the full path of what Enter, Ctrl-C
  // and the pencil act on — or the folder the cursor is in when its column is empty.
  std::string current_path() const {
    RolltuiStr sel{};
    int is_dir = 0;
    std::string out;
    if (rolltui_windows_picker_selected(windows, kPicker, 10, &sel, &is_dir) && sel.n) out.assign(sel.p, sel.n);
    rolltui_str_free(&sel);
    return out.empty() ? current_dir() : out;
  }
  std::string current_dir() const {
    std::string where = root;
    RolltuiStr d{};
    if (rolltui_windows_picker_dir(windows, kPicker, 10, &d)) where.assign(d.p ? d.p : "", d.n);  // "" in the top column: the root's own place
    rolltui_str_free(&d);
    return where.empty() ? std::string("/") : where;
  }

  // ---- find: what matches under the folder the cursor is in ----------------------------------
  // A FUZZY match, the shape every quick-open uses: every character of the query in order, case
  // folded; a run of adjacent hits and a hit at the start of a name or after a separator score
  // higher; a shorter path wins a tie. Not a regex and not a substring: "onetxt" finds one.txt.
  static int fuzzy(const std::string& hay, const std::string& q) {
    if (q.empty()) return 0;
    int score = 0;
    std::size_t at = 0;
    bool prev_hit = false;
    for (std::size_t i = 0; i < hay.size(); ++i) {
      if (at < q.size() && std::tolower(static_cast<unsigned char>(hay[i])) == std::tolower(static_cast<unsigned char>(q[at]))) {
        score += 1 + (prev_hit ? 3 : 0) + (i == 0 || hay[i - 1] == '/' || hay[i - 1] == '-' || hay[i - 1] == '_' || hay[i - 1] == '.' ? 2 : 0);
        ++at;
        prev_hit = true;
      } else {
        prev_hit = false;
      }
    }
    return at == q.size() ? score * 100 - static_cast<int>(std::min<std::size_t>(hay.size(), 99)) : -1;
  }
  // WALKS under `under`, bounded: eight levels, a few thousand entries, symlinked folders not
  // followed, dotfiles as the setting says. A bound is what keeps a find at / a moment, not a wait.
  void walk(const std::string& under, const std::string& rel, int depth, std::size_t& budget, const std::string& q) {
    if (depth > 8 || budget == 0) return;
    DIR* d = opendir(under.c_str());
    if (!d) return;
    std::vector<std::string> subs;
    while (const dirent* e = readdir(d)) {
      const std::string name = e->d_name;
      if (name == "." || name == "..") continue;
      if (!opt.hidden && name[0] == '.') continue;
      if (budget == 0) break;
      --budget;
      const std::string full = under + "/" + name;
      const std::string shown = rel.empty() ? name : rel + "/" + name;
      struct stat st {};
      const bool folder = lstat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
      const int s = fuzzy(shown, q);
      if (s >= 0) found.push_back(Match{full, shown, s, folder});
      if (folder) subs.push_back(name);
    }
    closedir(d);
    for (const std::string& name : subs) walk(under + "/" + name, rel.empty() ? name : rel + "/" + name, depth + 1, budget, q);
  }
  void find(const std::string& query) {
    find_query = query;
    find_under = current_dir();
    found.clear();
    if (query.empty()) { hint = "type something to find"; return; }
    std::size_t budget = 4000;
    walk(find_under, "", 0, budget, query);
    std::stable_sort(found.begin(), found.end(), [](const Match& a, const Match& b) { return a.score != b.score ? a.score > b.score : a.shown < b.shown; });
    if (found.size() > 200) found.resize(200);
    hint = budget == 0 ? "found among the first few thousand entries only" : "";  // the dialog says the rest
    rolltui_window_stack_push_popup(stack, layout, "matches", 7);
    matches_dirty = true;
  }
  // THE DIALOG'S LIST: the menu's rows are the matches, the menu file itself holding none. Each
  // row's id is the path, so choosing it is going there.
  void fill_matches() {
    RolltuiMenu* m = rolltui_windows_menu_at(windows, "matches", 7);
    if (!m) return;
    RolltuiMenuItem* rt = rolltui_menu_root(m);
    rolltui_menu_list_release(&rt->children);
    const std::string title = (found.empty() ? "nothing matched '" : "matches for '") + find_query + "' under " + find_under;
    rolltui_str_set(&rt->label, title.data(), title.size());
    for (const Match& x : found) {
      RolltuiMenuItem* it = rolltui_menu_list_add(&rt->children);
      rolltui_menu_item_set(it, ROLLTUI_MENU_ACTION, x.path.data(), x.path.size(), x.shown.data(), x.shown.size(), x.folder ? "folder" : "", x.folder ? 6 : 0);
    }
    if (found.empty()) {
      RolltuiMenuItem* it = rolltui_menu_list_add(&rt->children);
      const char* none = "no name under this folder has those letters in that order";
      rolltui_menu_item_set(it, ROLLTUI_MENU_SECTION, "none", 4, none, std::strlen(none), nullptr, 0);
    }
    rolltui_menu_reset(m);
  }
  // A MATCH CHOSEN: a folder is entered; a file is selected in its folder. The columns take the
  // focus back.
  void go_to_match(const std::string& path) {
    struct stat st {};
    const bool folder = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    if (folder) { jump(path); }
    else {
      const std::size_t slash = path.rfind('/');
      root = slash == std::string::npos ? "/" : path.substr(0, slash ? slash : 1);
      picker_go(path);
      hint.clear();
    }
    focus_base();
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
    if (matches_dirty) { fill_matches(); matches_dirty = false; }
    // THE PATH LINE SHOWS THE FOLDER THE CURSOR IS IN, unless a person is editing it.
    if (focused_content() != "input:path") {
      const std::string cur = current_path();
      if (cur != path_shown) {
        path_shown = cur;
        if (RolltuiInput* in = rolltui_windows_input(windows, "path", 4)) rolltui_input_set_text(in, cur.data(), cur.size());
      }
      if (cur != crumbs_for) build_crumbs(cur);
    }
    rolltui_windows_autosize(windows, stack, area());
    rolltui_windows_layout(windows, stack, area());
    // THE WINDOW REPORT IS THE APP AUTHOR'S CHANNEL — it names a window and a content string —
    // so it goes to the developer's stream, once per change, and never onto the screen a person
    // reads; what a person needs (a folder that cannot be read) the status line says in its
    // own words.
    {
      std::string now;
      if (rolltui_windows_report_count(windows) != 0) {
        RolltuiStr s{};
        rolltui_windows_report_summary(windows, &s);
        now.assign(s.c_str(), s.size());
        rolltui_str_free(&s);
      }
      if (now != note) { note = now; if (headless && !note.empty()) std::fprintf(stderr, "windows: %s\n", note.c_str()); }
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
      // A BUNDLE is a directory to the file system and an application to a person: never
      // entered, opened by the group `apps` names — the system opener, or the command line.
      const bool bundle = folder && name.size() > 4 && name.compare(name.size() - 4, 4, ".app") == 0;
      if ((folder && !bundle) || path.empty()) { chosen = path; exit_code = 0; quit = true; }
      else if (opt.leave) to_command_line(path);
      else if (!bundle && is_executable(path)) {
        // AN EXECUTABLE — a binary, a script with the x bit — by the `exec` setting: to the command
        // line so arguments can follow (the default; a picker never runs anything unasked), run
        // here on this terminal, or the system opener.
        if (opt.exec == Exec::Run) hint = open_with(kRunProgram, path) ? "ran " + name : "could not run " + name;
        else if (opt.exec == Exec::Open) hint = open_with(kSystemProgram, path) ? "opened " + name : "could not open " + name;
        else to_command_line(path);
      } else {
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
    else if (id == kRunProgram) cmd = {path};  // the executable itself, on this terminal
    else if (!p) return false;
    else if (p->exe && installed.on_path.count(id)) {
      cmd = {p->exe};
      if (p->arg) cmd.push_back(p->arg);
      cmd.push_back(path);
    } else if (p->bundle) cmd = {"open", "-a", p->bundle, path};
    else return false;
    const bool takes_terminal = ((p && p->terminal) || id == kRunProgram) && !stand_in;
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
    // A panel this screen declares is the library's to open — see
    // `rolltui_window_stack_action_popup`. `details`, `help`, `theme` and `keys` are four lines
    // this file does not have.
    else if (rolltui_window_stack_action_popup(stack, layout, action.data(), action.size())) {
      if (action == "app.menu") menu_dirty = true;
    }
    else if (action == "app.hidden") set_hidden(!opt.hidden);
    else if (action == "app.sort") {
      // SIX STATES, in the menu's order: each key one way, then the other.
      if (!opt.reversed) set_sort(opt.sort, true);
      else set_sort(opt.sort == Sort::Name ? Sort::Size : opt.sort == Sort::Size ? Sort::Modified : Sort::Name, false);
    }
  }

  // The three settings, each changed in ONE place whether a chord or the menu asked, and saved.
  // EVERY SETTING CHANGED SAYS SO on the status line, in the words the menu uses, whether it
  // came from the menu, a chord or a click on the bar.
  void set_hidden(bool on) {
    opt.hidden = on;
    apply_picker_options();
    hints_built = false;  // the bar says the state
    hint = opt.hidden ? "dotfiles shown" : "dotfiles hidden";
    save_settings();
  }
  void set_sort(Sort s, bool reversed) {
    opt.sort = s;
    opt.reversed = reversed;
    apply_picker_options();
    hints_built = false;
    hint = "sorted by " + sort_words(opt.sort, opt.reversed);
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
    // A PRESS ON A KEY HINT is that key: the bar answers with the action, and it runs as the
    // chord would — the global scope, so it works wherever the focus is.
    if (e.kind == ROLLTUI_EVENT_MOUSE && e.mouse.kind == RolltuiMouseEvent::Kind::Press) {
      std::size_t n = 0;
      if (const char* a = rolltui_hint_bar_hit(hints, e.mouse.x, e.mouse.y, &n)) { run_action(std::string(a, n)); return; }
      if (!editing_path() && rolltui_window_stack_depth(stack) == 1 && press_on_crumbs(e.mouse.x, e.mouse.y)) return;
    }
    // The app's OWN scope first, so a global chord works wherever the focus is — roll's rule —
    // EXCEPT a bare printable key while a line is being typed into: that is the text's.
    if (e.kind == ROLLTUI_EVENT_KEY) {
      std::size_t len = 0;
      const bool typing = e.key.key == ROLLTUI_KEY_CHAR && !e.key.ctrl && !e.key.alt && focused_content().rfind("input:", 0) == 0;
      if (const char* a = typing ? nullptr : rolltui_bindings_action_for(bindings, &e.key, "app", 3, &len)) {
        if (len != 0) { run_action(std::string(a, len)); return; }
      }
      // THE CLOSE KEY IN THE PATH LINE OR THE FIND FIELD hands the focus back to the columns —
      // the path put back as it was — the way it closes a popup: the same chord, read from the
      // stack's own table, so a rebinding moves both.
      const std::string fc = focused_content();
      if (rolltui_window_stack_depth(stack) == 1 && (fc == "input:path" || fc == "input:find")) {
        std::size_t al = 0;
        const char* a = rolltui_bindings_action_for(bindings, &e.key, "stack", 5, &al);
        const char* close = rolltui_stack_default_actions()->close_popup;
        if (a && al == std::strlen(close) && std::memcmp(a, close, al) == 0) {
          if (fc == "input:path") {
            if (RolltuiInput* in = rolltui_windows_input(windows, "path", 4)) rolltui_input_set_text(in, path_shown.data(), path_shown.size());
          }
          focus_base();
          return;
        }
      }
    }
    focused_before = focused_content();
    RolltuiStr window{};
    const unsigned char kind =
        rolltui_window_stack_route(stack, &e, area(), bindings, rolltui_stack_default_actions(), &window);
    const std::string target(window.c_str(), window.size());
    rolltui_str_free(&window);
    // THE PATH LINE, REACHED, IS SELECTED WHOLE — an address bar's rule: typing replaces the path,
    // an arrow key edits it.
    if (focused_before != "input:path" && focused_content() == "input:path") {
      if (RolltuiInput* in = rolltui_windows_input(windows, "path", 4)) rolltui_input_select_all(in);
    }
    if (kind != ROLLTUI_ROUTE_DELIVER) return;
    // The settings menu is the host's to drive, BEFORE the window table sees the event — the
    // menu widget would otherwise consume the key and the host would never learn what was chosen.
    if (RolltuiMenu* m = rolltui_windows_menu_at(windows, target.data(), target.size())) {
      RolltuiMenuEvent ev{};
      rolltui_menu_handle(m, &e, bindings, rolltui_menu_default_actions(), &ev);
      const std::string id(ev.id.p ? ev.id.p : "", ev.id.n);
      if (target == "matches") {
        if (ev.kind == ROLLTUI_MENU_EVENT_ACTIVATE && !id.empty() && id[0] == '/') {
          rolltui_window_stack_pop(stack);
          go_to_match(id);
        }
      } else if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && id == "sort") {
        const std::string v(ev.value.p ? ev.value.p : "", ev.value.n);
        Sort s = Sort::Name; bool rev = false;
        if (sort_from_id(v, s, rev)) set_sort(s, rev);
      } else if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && (id == "show_size" || id == "show_modified")) {
        Show& which = id == "show_size" ? opt.show_size : opt.show_modified;
        which = show_from(std::string(ev.value.p ? ev.value.p : "", ev.value.n));
        apply_picker_options();
        hint = std::string(id == "show_size" ? "sizes" : "modified dates") + (which == Show::Always ? ": always shown" : which == Show::Never ? ": never shown" : ": shown with the sort");
        save_settings();
      } else if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && id == "theme") {
        const std::string name(ev.value.p ? ev.value.p : "", ev.value.n);
        RolltuiThemePresetReport trep{};
        if (theme_store && rolltui_preset_store_load(theme_store, name.data(), name.size(), &trep, 1)) hint = "theme: " + name;
        else hint = "could not load the theme " + name;
        rolltui_theme_preset_report_release(&trep);
        menu_dirty = true;
      } else if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && id == "keys") {
        const std::string name(ev.value.p ? ev.value.p : "", ev.value.n);
        RolltuiBindingsPresetReport brep{};
        if (keys_store && rolltui_preset_store_load(keys_store, name.data(), name.size(), &brep, 1)) { rebuild_bindings(); hint = "key bindings: " + name; }
        else hint = "could not load the key bindings " + name;
        rolltui_bindings_preset_report_release(&brep);
        menu_dirty = true;
      } else if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && id.rfind("open_", 0) == 0) {
        opt.open_with[id.substr(5)] = std::string(ev.value.p ? ev.value.p : "", ev.value.n);
        hint = id.substr(5) + " opens with " + program_label(opt.open_with[id.substr(5)]);
        save_settings();
      } else if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && id == "exec") {
        opt.exec = exec_from(std::string(ev.value.p ? ev.value.p : "", ev.value.n));
        hint = std::string("executables: ") + (opt.exec == Exec::Run ? "run here" : opt.exec == Exec::Open ? "the system opener" : "to the command line");
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
        hint = opt.copy_relative ? "paths: relative to where the command line lands" : "paths: absolute";
        save_settings();
      } else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "hidden") set_hidden(ev.checked != 0);
      else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "motion") set_motion(ev.checked != 0);
      else if (ev.kind == ROLLTUI_MENU_EVENT_TOGGLE && id == "sparkle") { opt.sparkle = ev.checked != 0; hint = opt.sparkle ? "sparkle on" : "sparkle off"; save_settings(); }
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
    // THE BREADCRUMB over the path line's row while nobody is editing it: the parts in the value
    // colour, the separators and the ellipsis muted. While editing, the input draws itself.
    RolltuiRect pr{};
    if (!editing_path() && rolltui_window_stack_depth(stack) == 1 && path_rect(pr)) {
      // ONE BAND THE ROW'S WHOLE WIDTH, the panel's, the parts and the separators and the pencil
      // all on it — the title row's rule, one row up.
      const RolltuiStyle band = style(ROLLTUI_ROLE_PANEL_BACKGROUND);
      RolltuiStyle sep = style(ROLLTUI_ROLE_LABEL), part = style(ROLLTUI_ROLE_VALUE), muted = style(ROLLTUI_ROLE_TEXT_MUTED);
      sep.bg = band.bg; part.bg = band.bg; muted.bg = band.bg;
      rolltui_frame_fill(f, draw_scratch, RolltuiRect{pr.x, pr.y, pr.w, 1}, band, nullptr, 0);
      crumbs_used = rolltui_hint_bar_draw(crumbs, f, draw_scratch, pr.x, pr.y, pr.w, sep, part, muted, ambiguous ? 1 : 0);
    } else {
      rolltui_hint_bar_clear(crumbs);  // nothing to hit while the line is text
      crumbs_for.clear();
    }
    rolltui_frame_fill(f, draw_scratch, RolltuiRect{0, h - 1, w, 1}, style(ROLLTUI_ROLE_PANEL_BACKGROUND), nullptr, 0);
    // THE APP'S NAME sits at the right end of the status line — not on a border, where it read
    // as the columns' title — and a NOTE takes its place: drawn there, held, faded out, and the
    // name fades back in. Both are drawn below, where the line is laid out.
    // NAMED FACTS, DRAWN AS FACTS: the names muted and the answers bright, the same two roles
    // the columns above use. `status_rows` is reset and refilled rather than rebuilt, so a
    // frame that says nothing new allocates nothing to say it.
    RolltuiPickerStatus ps{};
    const bool have_picker = rolltui_windows_picker_status(windows, kPicker, 10, &ps) != 0;
    status_rows.reset();
    if (!hints_built) build_keys_hint();
    status_facts.reset();
    // WHAT IS NOT VISIBLE ELSEWHERE: the path line has the folder and the columns are on screen,
    // so the line carries a problem first (a folder that cannot be read, a bad path), then the
    // counts and the settings a glance cannot tell.
    if (have_picker && ps.error.n) rolltui_rows_add(&status_facts, "", 0, ps.error.p, ps.error.n);
    if (have_picker) { last_entries = ps.entries; last_column = ps.column; last_columns = ps.columns; }
    // WHAT CAN BE TAKEN NOW: with a popup up a press on the line is the popup's (it closes a
    // level), so every hint is muted; copy needs the columns focused and something under the
    // cursor.
    {
      const bool popup = rolltui_window_stack_depth(stack) > 1;
      RolltuiStr sel{};
      int is_dir = 0;
      const bool can_copy = !popup && focused_content() == "filepicker" && have_picker && rolltui_windows_picker_selected(windows, kPicker, 10, &sel, &is_dir) != 0 && sel.n != 0;
      rolltui_str_free(&sel);
      for (const char* a : {"app.help", "app.menu", "app.sort", "app.hidden", "app.details"}) rolltui_hint_bar_enable(hints, a, std::strlen(a), popup ? 0 : 1);
      rolltui_hint_bar_enable(hints, "picker.copy", 11, can_copy ? 1 : 0);
    }
    rolltui_picker_status_release(&ps);
    // THE LINE: the hint bar from the left (its own draw, so a press can be answered; hints that
    // do not fit are left out whole), the facts after it, and at the RIGHT END the app's name —
    // or, in its place, the note: shown, held two seconds, faded out over the next half, after
    // which the name fades back in over a third. The right end takes what it needs; the hints
    // squeeze as they do beside anything else.
    {
      const RolltuiStyleColor ground = style(ROLLTUI_ROLE_PANEL_BACKGROUND).bg;
      if (hint != hint_shown) { hint_shown = hint; hint_since = now_ms; }
      int right_w = 7;  // the name's cells
      if (!hint.empty()) {
        const unsigned long long age = now_ms >= hint_since ? now_ms - hint_since : 0;
        if (now_ms != 0 && age >= kHintHoldMs + kHintFadeMs) { hint.clear(); hint_shown.clear(); title_back_since = now_ms; }
      }
      if (!hint.empty()) {
        const unsigned long long age = now_ms >= hint_since ? now_ms - hint_since : 0;
        const double keep = now_ms == 0 || age < kHintHoldMs ? 1.0 : 1.0 - static_cast<double>(age - kHintHoldMs) / static_cast<double>(kHintFadeMs);
        const int nw = std::min(rolltui_frame_text_width(draw_scratch, hint.data(), hint.size(), ambiguous ? 1 : 0), std::max(w - 4, 0));
        right_w = nw;
        RolltuiStyle note_style;
        { const RolltuiStyle v = style(ROLLTUI_ROLE_VALUE); rolltui_style_fade(&v, ground, keep, &note_style); }
        rolltui_frame_put_text(f, draw_scratch, w - 1 - nw, h - 1, hint.data(), hint.size(), note_style, nw, ambiguous ? 1 : 0, 0);
      } else {
        const unsigned long long since = now_ms >= title_back_since ? now_ms - title_back_since : kTitleBackMs;
        const double keep = now_ms == 0 || title_back_since == 0 || since >= kTitleBackMs ? 1.0 : static_cast<double>(since) / static_cast<double>(kTitleBackMs);
        RolltuiStyle name_style = style(ROLLTUI_ROLE_BORDER_ACTIVE);  // the main border's colour…
        name_style.bg = ground;                                        // …on the line's own ground
        RolltuiStyle name_faded;
        rolltui_style_fade(&name_style, ground, keep, &name_faded);
        rolltui_frame_put_text(f, draw_scratch, w - 8, h - 1, "dirktui", 7, name_faded, 7, ambiguous ? 1 : 0, 0);
      }
      const int right = w - 2 - right_w;
      int at = 1;
      if (at < right) at += rolltui_hint_bar_draw(hints, f, draw_scratch, at, h - 1, right - at, style(ROLLTUI_ROLE_VALUE), style(ROLLTUI_ROLE_LABEL), style(ROLLTUI_ROLE_TEXT_MUTED), 0);
      at += 2;
      if (at < right)
        rolltui_frame_put_fields(f, draw_scratch, at, h - 1, &status_facts, style(ROLLTUI_ROLE_LABEL), style(ROLLTUI_ROLE_VALUE), right - at, 0);
    }
    apply_effects(f);
  }

  // The one line every host has: after the whole screen composed and before the diff, the
  // theme's motion is applied to whatever was marked. Also run for a headless frame, so a
  // self-test can read what a tick touched.
  void apply_effects(RolltuiFrame* f) {
    last_fx = RolltuiEffectReport{};
    last_marks = rolltui_frame_mark_count(f);
    if (!opt.sparkle || rolltui_frame_mark_count(f) == 0 || !effects || rolltui_effect_map_empty(effects)) return;
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
               "       dirktui --version                   the version\n"
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
    // ALREADY ON THE PATH — a Homebrew install, a system package — needs no link: the shell finds
    // it where it is, and a link to a binary a package manager may move would break later.
    bool on_path = false;
    if (const char* pv = std::getenv("PATH")) {
      std::istringstream in(pv);
      for (std::string dir; std::getline(in, dir, ':');) {
        // A `.local/bin` on the PATH is a link this command (or a person) made, not a package:
        // it is what install writes, never what makes install unnecessary.
        if (dir.empty() || dir == bin_dir || (dir.size() >= 11 && dir.compare(dir.size() - 11, 11, "/.local/bin") == 0)) continue;
        char real[PATH_MAX];
        const std::string cand = dir + "/dirktui";
        if (realpath(cand.c_str(), real) && self == real) { on_path = true; break; }
      }
    }
    if (on_path) {
      std::fprintf(stderr, "dirktui: already on your PATH at %s; no link made\n", self.c_str());
    } else {
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
  if (argc >= 2 && std::string(argv[1]) == "--version") {
#ifdef DIRKTUI_VERSION
    std::printf("dirktui %s\n", DIRKTUI_VERSION);
#else
    std::printf("dirktui (unversioned build)\n");
#endif
    return 0;
  }
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
    RolltuiStr mm{};
    if (rolltui_app_file(argv[0], "dirktui", "matches", dirktui_kAppFiles, dirktui_kAppFileCount, &mm, nullptr))
      app.matches_json.assign(mm.p ? mm.p : "", mm.n);
    rolltui_str_free(&mm);
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
      app.bindings_json = text;
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
      // NOT the undeliverable ones: the terminal has not been asked what it speaks yet, and a
      // chord judged against no terminal at all is a wrong answer printed with confidence —
      // Ctrl-. arrives fine on a terminal with a key protocol. They are judged in
      // `terminal_ready`, on the protocol negotiated, and said on the status line.
      const std::size_t judged_later = brep.undeliverable_n;
      brep.undeliverable_n = 0;
      rolltui_bindings_report_summary(&brep, &why);
      brep.undeliverable_n = judged_later;  // back for the release
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
      // A FRAME BETWEEN EVENTS, as the live loop has: a popup opened by one key has its widget
      // built at the next sync, and the key after must find it there — and DRAWN, because a
      // press on the status line is answered by where the hint bar landed on the last draw.
      RolltuiSwap* between = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
      app.render_into(rolltui_swap_begin(between, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND)));
      for (const rolltui_selftest::Step& st : rolltui_selftest::scripted_keys(keys_spec, app.w, app.h)) {
        app.now_ms = st.ms;
        if (st.tick) { moving = true; continue; }
        RolltuiEvent ev = st.ev;
        if (ev.kind == ROLLTUI_EVENT_PASTE) { ev.text = st.owned_text.data(); ev.text_len = st.owned_text.size(); }  // the step owns the pasted bytes
        app.handle(ev);
        app.settle();  // as the live loop settles after each batch: a copy's note appears at the next frame
        if (app.quit) break;
        app.prepare();
        app.render_into(rolltui_swap_begin(between, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND)));
      }
      rolltui_swap_free(between);
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
    // The picker's counts, for a test: the columns are on the screen and a person reads them
    // there, so the status line does not say "column 2/3" and this line does.
    std::fprintf(stderr, "picker: entries=%zu column %zu/%zu\n", app.last_entries, app.last_column, app.last_columns);
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
  app.terminal_ready();  // the key protocol is negotiated: judge the chords against it
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
