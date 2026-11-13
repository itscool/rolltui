// chat_test.cpp — `rolltui-chat`, the chat-shaped host, driven whole through its self-test build.
//
// The app exists so that the part of the public surface only a router outside this tree used to call is
// called by a program in it: the settings layer with a host's own environment prefix, the stores read for a
// listing, the help document from the live table, two registered kinds, a placeholder that follows the
// state, a note under the prompt, a reply stripped of its escapes. Each is asserted here through what the
// person sees — a frame — and each claim that a frame could satisfy by showing NOTHING has its control
// beside it: the same run without the step, or with the step's opposite.
//
// The reply streams on the SCRIPT'S clock (`selftest/script.hpp`: every key moves it one second; `Tick:MS`
// moves it MS), so a frame mid-reply is a script away and the same on every machine.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui_test.hpp"

using namespace rolltui_test;
using namespace testkit;
namespace fs = std::filesystem;

#ifndef ROLLTUI_CHAT_BIN
#error "ROLLTUI_CHAT_BIN must name the rolltui-chat-selftest binary"
#endif
#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui"
#endif

namespace {

std::string run(const std::string& cmd) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return out;
  char buf[4096];
  while (std::size_t n = std::fread(buf, 1, sizeof buf, p)) out.append(buf, n);
  pclose(p);
  return out;
}
bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
std::vector<std::string> lines_of(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  for (std::string l; std::getline(in, l);) out.push_back(l);
  return out;
}
// The row of the first frame line containing `text`, or -1.
int row_of(const std::string& frame, const std::string& text) {
  const std::vector<std::string> ls = lines_of(frame);
  for (std::size_t i = 0; i < ls.size(); ++i)
    if (has(ls[i], text)) return static_cast<int>(i);
  return -1;
}
// The row of the second line that STARTS a box (┌): the window under the transcript, the prompt.
int second_box_top(const std::string& frame) {
  const std::vector<std::string> ls = lines_of(frame);
  int seen = 0;
  for (std::size_t i = 0; i < ls.size(); ++i)
    if (ls[i].rfind("\xE2\x94\x8C", 0) == 0 && ++seen == 2) return static_cast<int>(i);
  return -1;
}
std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}

fs::path g_home;  // ROLL_CONFIG_DIR for the runs that do not say otherwise

// One frame: stdout (the frame) and stderr (the title, any gap report) together. `env` is prefixed to the
// command as `NAME=value` words; `home` is this run's own configuration directory.
std::string frame(const std::string& keys, const std::string& size = "100x24", const std::string& env = "",
                  const std::string& extra = "", const fs::path& home = g_home) {
  const std::string cmd = "ROLL_CONFIG_DIR='" + home.string() + "' " + env + " '" + ROLLTUI_CHAT_BIN + "' --frame " + size +
                          " --keys '" + keys + "' " + extra + " 2>&1";
  return run(cmd);
}

}  // namespace

int main() {
  // NOTHING THE PERSON'S SHELL SAYS reaches a run: every CHAT_ variable a run needs is stated on its line.
  for (const char* n : {"CHAT_THEME", "CHAT_LAYOUT", "CHAT_THEME_MODE", "CHAT_COLOR_DEPTH", "CHAT_BINDINGS"}) ::unsetenv(n);
  const char* tmp = std::getenv("TMPDIR");
  const fs::path scratch = fs::path(tmp && *tmp ? tmp : "/tmp") / ("rolltui_chat_test_" + std::to_string(getpid()));
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  g_home = scratch / "home";

  // ---- the screen names nothing this app lacks, and the check can see a lack -----------------
  {
    const std::string clean = frame("");
    check(!has(clean, "missing") && has(clean, "Message the pretend assistant"),
          "the shipped `default` screen names nothing rolltui-chat does not provide (its two kinds are registered)");
    const fs::path odd = scratch / "odd.json";
    std::ofstream(odd) << R"({ "name": "odd", "root": { "row": [ { "id": "x", "content": "nonesuch", "focusable": true } ] } })";
    const std::string gap = frame("", "100x24", "CHAT_LAYOUT='" + odd.string() + "'", "", scratch / "home-odd");
    check(has(gap, "missing"), "CONTROL: a screen naming a kind nobody provides IS reported at start, so the silence above is a clean screen");
  }

  // ---- the first frame ------------------------------------------------------------------------
  {
    const std::string f = frame("");
    check(has(f, "transcript") && has(f, "status") && has(f, "messages  0 you, 0 assistant"),
          "the first frame: the transcript, the prompt and the status panel, nothing said yet");
    check(has(f, "title: rolltui-chat \xC2\xB7 0 messages"), "the window's title says how much has been said");
    check(has(f, "Enter sends \xC2\xB7 F1 help"), "the note under the prompt says what Enter does");
  }

  // ---- a reply streams on the script's clock --------------------------------------------------
  {
    const std::string before = frame("Type:hello Enter");
    check(has(before, "thinking\xE2\x80\xA6") && !has(before, "You wrote") && has(before, "title: rolltui-chat \xC2\xB7 replying"),
          "Enter at a clock with no piece due: the note says thinking, nothing has arrived, the title says replying");
    check(has(before, "replying: Enter queues a message") && !has(before, "Message the pretend assistant"),
          "…and the placeholder changed with the state (the options were compared, then replaced)");
    const std::string part = frame("Type:hello Enter Tick:250");
    check(has(part, "You wrote 1 word") && !has(part, "I am a pretend") && has(part, "replying\xE2\x80\xA6"),
          "250 ms in: the first pieces have arrived and the rest have not, and the note says replying");
    const std::string done = frame("Type:hello Enter Tick:5000");
    check(has(done, "I am a pretend assistant") && has(done, "1 you, 1 assistant") && has(done, "title: rolltui-chat \xC2\xB7 2 messages"),
          "5 s in: the whole reply, counted, and the title follows");
    check(has(done, "Message the pretend assistant"), "…and the placeholder is back to the idle one");
  }

  // ---- what a reply may not do ----------------------------------------------------------------
  {
    const std::string clean = frame("Type:show_me_colour Enter Tick:5000");
    check(has(clean, "paint the screen red and") && !has(clean, "[31m") && !has(clean, "pwned"),
          "a reply's escape sequences are stripped before it is shown: its text arrives, its colour and its title do not");
    const std::string raw = frame("Type:show_me_colour Enter Tick:5000", "100x24", "", "--no-strip");
    check(has(raw, "[31m") && has(raw, "pwned"),
          "CONTROL: the same reply unstripped carries both, so the absence above is the stripping");
    const std::string code = frame("Type:some_code Enter Tick:5000");
    check(has(code, "int add(int a, int b) {") && has(code, "\xE2\x94\x8C c "), "a fenced reply is drawn as a code block");
  }

  // ---- the approval kind: a question that waits for a yes ---------------------------------------
  {
    const std::string ask = frame("Type:!make_all Enter");
    check(has(ask, "approval") && has(ask, "$ make all") && has(ask, "answer the que") &&
              has(ask, "title: rolltui-chat \xC2\xB7 waiting for you"),
          "`!make all` opens the host's own `approval` kind with the command in it, and the note and title wait");
    // The prompt's top border is the SECOND line that starts a box (the transcript's is the first). On the
    // row of the question's box, the prompt is held exactly as tall as it; idle, the prompt is shorter.
    check(row_of(ask, "\xE2\x95\xAD approval") == second_box_top(ask) && second_box_top(ask) > 0,
          "the prompt is held as tall as the question's box, so the box covers the prompt and not the transcript");
    check(second_box_top(frame("")) > second_box_top(ask), "CONTROL: idle, the prompt is shorter, so its top is lower");
    check(has(frame("Type:!make_all Enter Type:y Tick:5000"), "Ran make all (pretend"), "y runs it (pretend), and the reply says so");
    check(has(frame("Type:!make_all Enter Type:n"), "declined: make all"), "n declines");
    const std::string esc = frame("Type:!make_all Enter Escape");
    check(has(esc, "declined: make all") && !has(esc, "$ make all"), "Escape closes the question, and closing it is a no");
  }

  // ---- stopping, and typing ahead ---------------------------------------------------------------
  {
    const std::string seven = "Type:one_two_three_four_five_six_seven Enter Tick:250";
    check(has(frame(seven + " Escape Tick:5000"), "(stopped)"), "Escape while a reply arrives stops it, and the reply says it was stopped");
    check(!has(frame(seven + " Tick:5000"), "(stopped)"), "CONTROL: the same reply left alone is not stopped");
    std::string many = "Paste:";
    for (int i = 0; i < 200; ++i) many += (i ? "_w" : "w") + std::to_string(i);
    const std::string queued = frame(many + " Enter Type:second Enter");
    check(has(queued, "queued: it is sent when the reply ends (1 waiting)"), "a message sent while a reply arrives is queued, and the note says so");
    const std::string after = frame(many + " Enter Type:second Enter Tick:5000 Tick:5000", "100x80");
    check(has(after, "\xE2\x80\xBA second") && has(after, "2 you, 2 assistant"), "…and it is sent when the reply ends, and answered");
  }

  // ---- the slash commands -----------------------------------------------------------------------
  {
    const std::string help = frame("Type:/help Enter", "150x70");
    check(has(help, "app:") && has(help, "write the message in $EDITOR") && has(help, "stop the reply"),
          "/help is the live table's document, and this app's own two actions are in it (their chords were suggested and taken)");
    check(has(frame("F1", "150x40"), "open the settings and commands menu"), "F1 opens the same document as a popup");
    check(has(frame("Type:/nope Enter"), "unknown command '/nope'"), "an unknown command is named");
    check(has(frame("Type:/terminal Enter"), "no terminal: this run draws headless"), "/terminal says there is no terminal when there is none");

    const std::string layouts = frame("Type:/layouts Enter", "150x40");
    check(has(layouts, "default      fits 60x8 or larger; shipped") && has(layouts, "stacked      fits any size;"),
          "/layouts reads each screen without switching to it, for the smallest terminal it fits");
    const std::string keys = frame("Type:/keys Enter", "150x40");
    check(has(keys, "* default") && has(keys, "the working table matches the shipped 'default'"),
          "/keys lists the presets and says whether the working table has drifted from the shipped one");
    check(has(frame("Type:/keys_app Enter", "150x40"), "Ctrl-X      write the message in $EDITOR"), "/keys app lists one scope's rows");
  }

  // ---- the themes: shipped, and yours -----------------------------------------------------------
  {
    const std::string themes = frame("Type:/themes Enter", "150x50");
    int shipped = 0, listed = 0;
    for (const auto& e : fs::directory_iterator(fs::path(ROLLTUI_SOURCE_DIR) / "presets" / "themes"))
      if (e.path().extension() == ".json") ++shipped;
    for (const std::string& l : lines_of(themes))
      if (has(l, " shipped ")) ++listed;
    check(listed == shipped && shipped > 1, "/themes lists every shipped theme (" + std::to_string(listed) + " of " + std::to_string(shipped) + ")");
    check(has(themes, "* default "), "…with the one in use marked");
    const fs::path home = scratch / "home-shadow";
    check(has(frame("Type:/themes_ember Enter", "150x30", "", "", home), "ember: shipped with rolltui"),
          "/themes ember reads a shipped theme without choosing it");
    fs::create_directories(home / "rolltui" / "themes");
    fs::copy_file(fs::path(ROLLTUI_SOURCE_DIR) / "presets" / "themes" / "ember.json", home / "rolltui" / "themes" / "ember.json");
    check(has(frame("Type:/themes_ember Enter", "150x30", "", "", home), "ember: yours, read instead of the shipped one"),
          "CONTROL: a file of the person's own under that name is said to be read instead");
  }

  // ---- the settings layer, with this host's prefix ----------------------------------------------
  {
    const fs::path home = scratch / "home-settings";
    const fs::path working = home / "rolltui" / "theme.working.json";
    const std::string set = frame("Type:/set_theme\\_mode_light Enter", "150x30", "", "", home);
    check(has(set, "theme_mode = light (in theme 'default')"), "/set changes a setting and names the preset it lives in");
    const std::string kept = slurp(working);
    check(has(kept, "light"), "…and keeps it: the working copy on disk says light");
    check(has(frame("Type:/config Enter", "150x30", "", "", home), "theme_mode   light        from the working copy"),
          "the next run's /config reads it back from the working copy");

    const std::string env = frame("Type:/config Enter", "150x40", "CHAT_THEME=mono CHAT_COLOR_DEPTH=16", "", home);
    check(has(env, "theme: 'mono' from the environment (CHAT_THEME), for this run only"), "a CHAT_ variable fills the run and says so at start");
    check(has(env, "theme        mono         from the environment (CHAT_THEME)") &&
              has(env, "color_depth  16           from the environment (CHAT_COLOR_DEPTH)"),
          "/config names the environment rung and the variable for each key it filled");
    check(has(env, "theme mono") || has(env, "theme     mono"), "…and the status panel draws in it");
    check(slurp(working) == kept, "…and NOTHING was written: the working copy is byte for byte what the last edit left");
    check(has(frame("Type:/set_bogus_x Enter", "150x30", "", "", home), "unknown setting 'bogus' (theme, layout, theme_mode, color_depth, bindings)"),
          "an unknown key is refused with the keys there are");
    const std::string purple = frame("Type:/set_theme\\_mode_purple Enter", "150x30", "", "", home);
    check(has(purple, "theme_mode must be auto | dark | light, not 'purple'") && !has(purple, "theme_mode = purple"),
          "a value outside the set is refused in the table's own words, not applied");
  }

  // ---- the palette and the menu -----------------------------------------------------------------
  {
    const std::string about = frame("Type:hi Enter Tick:5000 CtrlP Type:clear", "150x24");
    check(has(about, "about") && has(about, "/clear: start again"), "the palette's focused row is described in the status panel");
    check(has(about, "1 you, 1 assistant"), "CONTROL: there is a conversation to clear");
    check(has(frame("Type:hi Enter Tick:5000 CtrlP Type:clear Enter"), "0 you, 0 assistant"),
          "Enter on the palette's row runs the command it describes");
    const fs::path home = scratch / "home-menu";
    check(has(frame("CtrlP Type:ember Enter", "120x24", "", "", home), "theme     ember"), "the palette chooses a theme through the settings table");
    check(has(frame("", "120x24", "", "", home), "theme     ember"), "…and it is kept: the next run draws in it");
    const std::string one = frame("F2 Enter Enter Escape", "120x24");
    const std::string two = frame("F2 Enter Enter Escape Escape", "120x24");
    const std::string three = frame("F2 Enter Enter Escape Escape Escape", "120x24");
    check(has(frame("F2 Enter Enter", "120x24"), "menu \xE2\x80\xBA Settings \xE2\x80\xBA Theme"), "the menu opens a level per Enter");
    check(has(one, "menu \xE2\x80\xBA Settings ") && has(two, "\xE2\x95\xAD menu \xE2\x94\x80") && !has(three, "\xE2\x95\xAD menu"),
          "Escape goes back ONE level at a time, and only the top closes the menu");
  }

  // ---- the details kind, find, a small terminal, Ctrl-D ------------------------------------------
  {
    const std::string d = frame("F3", "100x14");
    check(has(d, "session") && has(d, "conversation") && has(d, "\xE2\x80\xA6 \xE2\x94\x82"),
          "F3 opens the host's `details` kind; a line too long for it ends in an ellipsis");
    const std::string down = frame("F3 PageDown", "100x14");
    check(!has(down, "conversation") && has(down, "terminal"), "PageDown scrolls it by its own height");

    const std::string find = frame("Type:alpha_beta Enter Tick:5000 CtrlF Type:beta");
    check(has(find, "find") && has(find, "> beta"), "Ctrl-F opens the find bar and takes what is typed");
    check(!has(frame("Type:alpha_beta Enter Tick:5000 CtrlF Type:beta Escape"), "> beta"), "Escape closes it");

    const std::string small = frame("Type:hi Enter Tick:5000", "50x20");
    check(!has(small, "\xE2\x94\x8C status") && has(small, "messages 1 you, 1 assistant"),
          "a terminal smaller than the screen gets the shipped `stacked` one");
    check(has(frame("Type:hi Enter Tick:5000"), "\xE2\x94\x8C status"), "CONTROL: at full size the status panel is back");

    check(!has(frame("CtrlD Type:after"), "after"), "Ctrl-D on an empty line quits");
    check(has(frame("Type:x Home CtrlD Type:after"), "after"), "CONTROL: Ctrl-D with a draft deletes, and the app runs on");
    check(has(frame("Type:what?"), "what?") && !has(frame("Type:what?"), "\xE2\x95\xAD help"),
          "? in a draft is typed, not help");
  }

  fs::remove_all(scratch);
  return report("rolltui_chat_test");
}
