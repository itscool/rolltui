//
// rolltui/examples/chat.cpp — `rolltui-chat`, the library's chat-shaped host, IN THIS TREE.
//
// A conversation with a PRETEND assistant: a transcript, a prompt under it, a status panel beside
// it, and the shipped `default` screen's popups. Every reply is made from what you typed, so a
// test can predict it — there is no model, no network and nothing to configure.
//
// WHY IT EXISTS. roll (a router outside this tree) was for a long time the only program that
// called a part of the public surface: the settings layer with a host's own environment prefix, a
// loop of the host's own built from the terminal primitives, a worker thread waking that loop, a
// note under the prompt, a placeholder that changes with the state, the terminal handed to an
// editor and taken back, registered kinds that draw a host's own question. A public function no
// program in this tree calls is a function nothing here can see break, so this one calls them,
// and `chat_test` and `live_terminal_test` run it.
//
// WHAT IT SHOWS, by the part of the API it reaches:
//   - THE LOOP, WRITTEN OUT. `rolltui_run` is the loop for most apps (paint, dirktui and the studio
//     use it). A host with threads and a lifecycle of its own writes the loop from the primitives
//     `rolltui_run` is made of — `_terminal_new`, `_poll`, `_present`, `_wake`, `_suspend`/`_resume`
//     — and this is the program in this tree that does. The reply is produced on ANOTHER thread,
//     which wakes the loop when a piece arrives; nothing on screen moves while it waits, so the loop
//     sleeps without a timeout and only the wake can deliver the reply.
//   - THE SETTINGS LAYER, with this host's prefix: `CHAT_THEME`, `CHAT_LAYOUT`, … fill a run without
//     being written (`/config` says which rung each value came from); `/set KEY VALUE` changes one and
//     keeps it; the menu's Settings level is the same five keys.
//   - THE STORES, read: `/themes`, `/layouts` and `/keys` list what is shipped and what is yours, where
//     each lives, and whether the working copy has drifted from the preset it came from.
//   - TWO REGISTERED KINDS, `approval` and `details`, the ones the shipped `default` layout names and
//     only a host can draw: a command the assistant wants to run (`!make` asks), and the session report.
//   - WHAT A REPLY MAY NOT DO: a reply's escape sequences are stripped before they are shown, so a
//     reply cannot retitle the terminal or paint it red (`colour` asks for one that tries).
//
// `rolltui-chat-selftest --frame WxH --keys "…"` draws one frame after a script (the vocabulary is
// `selftest/script.hpp`'s); the reply then streams on the SCRIPT'S clock, so `Enter Tick:5000` is a
// finished reply and `Enter` alone is one that has not started.
//
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#ifndef R_OK
#define R_OK 4
#endif
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "rolltui/rolltui.h"
#include "rolltui/rolltui_cpp.h"
#include "rolltui/str.hpp"

#ifdef ROLLTUI_SELFTEST
#include "rolltui/selftest/script.hpp"
#endif

using rolltui::StrView;

namespace {

// THIS APP'S MENU, carried in its own binary (the middle of the three menu rungs, so a person can
// shadow it with menus/main.json). The Settings level is the settings table's own five keys, one
// item per key named `settings.<key>`, so one branch applies them all; the Commands level is the
// slash commands, so the palette and the prompt reach one dispatcher.
constexpr const char* kMenu = R"({
  "id": "root", "label": "chat", "items": [
    { "id": "settings", "label": "Settings", "items": [
      { "id": "settings.theme", "label": "Theme", "kind": "choice",
        "description": "The colours. Yours and the shipped ones; /themes lists them." },
      { "id": "settings.theme_mode", "label": "Light or dark", "kind": "choice",
        "description": "Yours, kept across themes: auto follows the terminal.",
        "items": [ { "id": "auto", "label": "auto — follow the terminal" }, { "id": "dark" }, { "id": "light" } ] },
      { "id": "settings.color_depth", "label": "Colour depth", "kind": "choice",
        "description": "auto draws at what the terminal said it can.",
        "items": [ { "id": "auto" }, { "id": "truecolor" }, { "id": "256" }, { "id": "16" }, { "id": "mono" } ] },
      { "id": "settings.layout", "label": "Layout", "kind": "choice",
        "description": "Where the transcript, the prompt and the status panel go." },
      { "id": "settings.bindings", "label": "Key bindings", "kind": "choice",
        "description": "Which keys do what; /keys lists the live table." } ] },
    { "id": "commands", "label": "Commands", "items": [
      { "id": "cmd.help", "label": "Help", "action": "app.help",
        "description": "Every key this screen answers, from the live table." },
      { "id": "cmd.details", "label": "Session details", "action": "app.details",
        "description": "What this conversation has said, and how it is drawn." },
      { "id": "cmd.find", "label": "Find in the conversation", "action": "app.find",
        "description": "Type to highlight every match in the transcript." },
      { "id": "cmd.editor", "label": "Write the message in $EDITOR", "action": "app.editor",
        "description": "The terminal is handed to your editor and taken back." },
      { "id": "cmd.config", "label": "Show the settings",
        "description": "/config: each setting, and which rung its value came from." },
      { "id": "cmd.themes", "label": "List the themes",
        "description": "/themes: shipped and yours, and where each lives." },
      { "id": "cmd.layouts", "label": "List the layouts",
        "description": "/layouts: each screen and the smallest terminal it fits." },
      { "id": "cmd.keys", "label": "List the key bindings",
        "description": "/keys: the presets, and this screen's own keys." },
      { "id": "cmd.terminal", "label": "What the terminal is",
        "description": "/terminal: what was remembered about it, and what it answers now." },
      { "id": "cmd.clear", "label": "Clear the conversation",
        "description": "/clear: start again with an empty transcript." },
      { "id": "cmd.quit", "label": "Quit", "description": "/quit" } ] } ] }
)";

// THIS APP'S OWN TWO ACTIONS. A suggestion, never an override: a person's table that already says
// something about either keeps it. `app.stop` is Escape in the APP scope, and the app only takes it
// while a reply is arriving and nothing is open — every other Escape still closes what it closes.
constexpr RolltuiToolAction kOwnActions[] = {
    {"app.stop", "stop the reply", "escape"},
    {"app.editor", "write the message in $EDITOR", "ctrl+x"},
};

// The help popup and `/help` are ONE document: the live table's rows for these scopes, between a
// lead and a note that say what no chord says.
constexpr const char* kHelpScopes[] = {"app", "input", "transcript"};
constexpr const char* kHelpLead = "Enter sends; Alt-Enter starts a new line. Ctrl-D on an empty line quits.\n";
constexpr const char* kHelpNote =
    "commands: /help /config /set KEY VALUE /themes [NAME] /layouts [NAME] /keys [SCOPE] /terminal /clear /quit\n"
    "a message starting with ! asks to run it (pretend); one mentioning code or colour gets a reply that shows one.";

// The prefix a setting's environment variable carries in THIS host. The table names the tail
// ("THEME"); the host names the head, so two programs on one machine do not share a variable.
constexpr const char* kEnvPrefix = "CHAT_";

// The library's own spellings, by its own lists, rather than a second copy of them here.
constexpr const char* kDepthNames[] = {
#define CHAT_NAME_(lower, UPPER, Camel) lower,
    ROLLTUI_DEPTH_LIST(CHAT_NAME_)
};
constexpr const char* kModeNames[] = {ROLLTUI_MODE_LIST(CHAT_NAME_)};
constexpr const char* kProtocolNames[] = {ROLLTUI_PROTOCOL_LIST(CHAT_NAME_)
#undef CHAT_NAME_
};
constexpr const char* kFactSources[] = {"nothing said", "the environment", "the terminal", "remembered", "said outright"};

template <std::size_t N>
const char* name_of(const char* const (&names)[N], unsigned v) { return v < N ? names[v] : "?"; }

unsigned long long steady_ms() {
  return static_cast<unsigned long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Where a person's own presets live — the same rungs every app in this tree walks, so a theme chosen
// in one is the theme in all of them.
RolltuiStr user_presets_dir() {
  if (const char* d = std::getenv("ROLL_CONFIG_DIR"); d && *d) return RolltuiStr(d) + "/rolltui";
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return RolltuiStr(x) + "/roll/rolltui";
  const char* home = std::getenv("HOME");
  return RolltuiStr(home && *home ? home : ".") + "/.config/roll/rolltui";
}

RolltuiStr stripped(StrView s) {
  RolltuiStr out;
  if (s.empty()) return out;
  std::vector<char> buf(s.size());
  const std::size_t n = rolltui_u_strip_escape_sequences(s.data(), s.size(), buf.data());
  out.assign(buf.data(), n);
  return out;
}

std::size_t count_words(StrView s) {
  std::size_t n = 0;
  bool in = false;
  for (char c : s) {
    const bool space = c == ' ' || c == '\n' || c == '\t';
    if (!space && !in) ++n;
    in = !space;
  }
  return n;
}

bool mentions(StrView hay, StrView needle) { return hay.find(needle) != StrView::npos; }

// ---- the pretend assistant -------------------------------------------------------------------

// A REPLY IS A FUNCTION OF THE MESSAGE, so a test knows what will arrive. Three shapes besides the
// plain one, each the case a real model's output puts to a transcript: a fenced code block, prose
// carrying escape sequences (which must never reach the terminal), and a request to run something.
RolltuiStr reply_for(StrView msg) {
  RolltuiStr r;
  const std::size_t words = count_words(msg);
  if (mentions(msg, "code")) {
    r = "Here is a small function, fenced so the transcript draws it as code:\n\n"
        "```c\n"
        "int add(int a, int b) {\n"
        "  return a + b;\n"
        "}\n"
        "```\n\n"
        "It adds two numbers. That is all it does.";
    return r;
  }
  if (mentions(msg, "colour") || mentions(msg, "color")) {
    // A real model's text can carry a terminal's control sequences — pasted from a log, or put there
    // on purpose. These two would paint the rest of the screen red and rename the window.
    r = "This reply tried to paint the screen \x1b[31mred\x1b[0m and to rename the window"
        "\x1b]0;pwned\x07, and neither reached the terminal: a reply is stripped before it is shown.";
    return r;
  }
  rolltui::appendf(r, "You wrote **%zu word%s** (%zu bytes).\n\n> ", words, words == 1 ? "" : "s", msg.size());
  for (char c : msg) {
    r += c;
    if (c == '\n') r += "> ";
  }
  r += "\n\nI am a pretend assistant: every reply is made from what you typed, so a test can predict it.";
  return r;
}

// The reply in the pieces it arrives in: three words a piece, each keeping the space after it, so
// joining them gives the reply back exactly.
std::vector<RolltuiStr> pieces_of(StrView text) {
  std::vector<RolltuiStr> out;
  std::size_t start = 0, words = 0, i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] != ' ' && text[i] != '\n') ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n')) ++i;
    if (++words == 3 || i >= text.size()) {
      out.push_back(rolltui::own(text.substr(start, i - start)));
      start = i;
      words = 0;
    }
  }
  return out;
}

constexpr unsigned long long kThinkMs = 150;  // before the first piece
constexpr unsigned long long kPieceMs = 40;   // between pieces

// THE OTHER THREAD. The UI thread hands it a reply's pieces; it releases them on its own clock and
// WAKES the loop after each, because the loop is asleep with no timeout and nothing else will. In
// the self-test there is no thread and no terminal: `pump` releases what the SCRIPT'S clock says is
// due, so a frame mid-reply is a script away and the same on every machine.
class Responder {
 public:
  explicit Responder(bool threaded) : threaded_(threaded) {}
  Responder(const Responder&) = delete;
  Responder& operator=(const Responder&) = delete;
  ~Responder() { stop(); }

  // `term` is borrowed for the thread's whole life and must outlive `stop()`.
  void start(RolltuiTerminal* term) {
    term_ = term;
    if (threaded_) worker_ = std::thread([this] { run(); });
  }
  // THE TERMINAL IT WAKES, changed while it runs: the screen switching between inline and whole. The wake below happens under the
  // same lock, so once this returns the old terminal is never woken again and may be freed.
  void retarget(RolltuiTerminal* term) {
    std::lock_guard<std::mutex> lock(term_mu_);
    term_ = term;
  }
  void stop() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      quit_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  void begin(std::vector<RolltuiStr> pieces, unsigned long long now) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      job_ = std::move(pieces);
      job_start_ = now;
      next_ = 0;
      ++job_id_;
      has_job_ = true;
      outbox_.clear();
      finished_ = false;
    }
    cv_.notify_all();
  }
  void cancel() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      ++job_id_;  // anything the worker produces for the old job is dropped
      has_job_ = false;
      outbox_.clear();
      finished_ = false;
    }
    cv_.notify_all();
  }
  // The script's clock: release every piece due by `now`.
  void pump(unsigned long long now) {
    if (threaded_) return;
    std::lock_guard<std::mutex> lock(mu_);
    if (!has_job_) return;
    while (next_ < job_.size() && job_start_ + kThinkMs + kPieceMs * next_ <= now) outbox_ += job_[next_++];
    if (next_ == job_.size()) { finished_ = true; has_job_ = false; }
  }
  // What arrived since the last call, appended to `into`; true once the reply is complete.
  bool drain(RolltuiStr& into) {
    std::lock_guard<std::mutex> lock(mu_);
    into += outbox_;
    outbox_.clear();
    const bool f = finished_;
    finished_ = false;
    return f;
  }

 private:
  void run() {
    std::unique_lock<std::mutex> lock(mu_);
    while (!quit_) {
      cv_.wait(lock, [&] { return quit_ || has_job_; });
      if (quit_) break;
      const unsigned long long id = job_id_;
      std::vector<RolltuiStr> pieces = std::move(job_);
      job_.clear();
      has_job_ = false;
      for (std::size_t i = 0; i < pieces.size(); ++i) {
        const auto wait = std::chrono::milliseconds(i == 0 ? kThinkMs : kPieceMs);
        if (cv_.wait_for(lock, wait, [&] { return quit_ || job_id_ != id; })) break;
        outbox_ += pieces[i];
        if (i + 1 == pieces.size()) finished_ = true;
        lock.unlock();
        {
          std::lock_guard<std::mutex> t(term_mu_);
          if (term_) rolltui_terminal_wake(term_);  // thread-safe: one byte on the terminal's self-pipe
        }
        lock.lock();
      }
    }
  }

  const bool threaded_;
  std::mutex term_mu_;  // guards `term_` against `retarget`
  RolltuiTerminal* term_ = nullptr;
  std::thread worker_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::vector<RolltuiStr> job_;
  unsigned long long job_start_ = 0, job_id_ = 0;
  std::size_t next_ = 0;
  bool has_job_ = false, quit_ = false, finished_ = false;
  RolltuiStr outbox_;
};

// ---- the app ---------------------------------------------------------------------------------

// The events of one wake, COPIED: the terminal lends each one only for the length of its emit call,
// and handling one may resize the screen or stop the loop.
struct Pending {
  std::vector<RolltuiTermEvent> ev;
  std::vector<std::size_t> at;
  RolltuiStr text;
  static void collect(void* ctx, const RolltuiTermEvent* te) {
    Pending& p = *static_cast<Pending*>(ctx);
    p.ev.push_back(*te);
    p.at.push_back(te->text ? p.text.size() : static_cast<std::size_t>(-1));
    if (te->text) p.text.append(te->text, te->text_len);
  }
};

struct App {
  // APP LIFETIME, RELEASED IN ONE DESTRUCTOR — paint's shape.
  RolltuiContext* ctx = rolltui_context_new();
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  RolltuiEffectMap* effects = nullptr;
  RolltuiEffectScratch* effect_scratch = rolltui_effect_scratch_new();
  RolltuiDrawScratch* draw_scratch = rolltui_draw_scratch_new();
  RolltuiUnicodeScratch* u_scratch = rolltui_u_scratch_new();
  RolltuiWindows* windows = rolltui_windows_new(ctx);
  RolltuiWindowStack* stack = rolltui_window_stack_new();
  RolltuiComposeScratch* compose_scratch = rolltui_compose_scratch_new();
  RolltuiThemeStore* theme_store = nullptr;
  RolltuiLayoutStore* layout_store = nullptr;
  RolltuiBindingsStore* keys_store = nullptr;
  RolltuiSettings* settings = nullptr;
  RolltuiBindings* bindings = nullptr;  // OWNED: a copy of the bindings store's working table
  RolltuiLayout* layout = nullptr;      // OWNED: a copy of the layout store's working screen
  const RolltuiLayout* shown = nullptr; // what is on screen: `layout`, or the shipped narrow fallback
  unsigned long long theme_seen = 0, layout_seen = 0, keys_seen = 0;
  RolltuiDocument doc;
  // What an inline screen committed to the terminal's history, in order: a whole-screen layout chosen later shows it again.
  RolltuiDocument history;
  // How many entries at the front of `doc` the terminal's history already holds, while a whole screen shows them again: an inline
  // layout chosen after it takes them back without writing them twice.
  std::size_t written_before = 0;
  unsigned long long next_id = 0;

  // The terminal, and the double buffer, only in the interactive loop.
  RolltuiTerminal* term = nullptr;
  RolltuiSwap* swap = nullptr;
  Responder responder;
  int w = 100, h = 30;
  // INLINE: a layout that says `"inline": true` (the shipped `inline`) is drawn below the prompt in the normal screen, a finished
  // message left in the terminal's own history. The region is `fh` rows: the live entries and the prompt, taller while a popup is open.
  bool inline_mode = false;
  int fh = 0;
  unsigned char ambiguous = 0;
  unsigned long long now = 0;  // the clock this frame is drawn at
  bool quit = false;
  bool strip_replies = true;   // off only in the self-test's planted control

  // The conversation's state.
  std::size_t you_said = 0, assistant_said = 0, words_in = 0, words_out = 0;
  bool replying = false;
  bool first_piece = false;
  unsigned long long reply_since = 0;
  std::size_t open_entry = 0;  // the reply arriving into, while `replying`
  RolltuiStr open_raw;         // ...and its text as it ARRIVED, before stripping
  std::vector<RolltuiStr> queued;  // typed while a reply arrived; sent when it ends
  RolltuiStr approval_cmd;     // non-empty while the assistant waits for a yes or a no
  RolltuiStr hint;             // one line the app has to say, under the prompt
  bool editor_wanted = false;
  int details_top = 0;

  bool headless = false;  // a --frame dump: one moment, drawn still
  explicit App(bool threaded) : responder(threaded), headless(!threaded) {
    rolltui_context_set_library_defaults(ctx);
    effects = rolltui_theme_builtin_fill("default-dark", 12, styles, ROLLTUI_ROLE_COUNT);
    // THE TWO KINDS the shipped `default` screen names and only a host can draw. The name goes to the
    // layout vocabulary (a source is FORBIDDEN: there is one of each), the factory to the window table.
    rolltui_widget_kind_register(ctx, "approval", 8, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    rolltui_context_register_kind(ctx, "approval", 8, approval_factory, this, nullptr);
    rolltui_widget_kind_register(ctx, "details", 7, ROLLTUI_SOURCE_FORBIDDEN, "", 0);
    rolltui_context_register_kind(ctx, "details", 7, details_factory, this, nullptr);
    rolltui_context_add_menu(ctx, "main", 4, kMenu, std::strlen(kMenu));
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() {
    rolltui_child_free(running);  // a command still running is stopped
    rolltui_ansi_free(running_out);
    responder.stop();
    rolltui_settings_free(settings);
    rolltui_bindings_store_free(keys_store);
    rolltui_layout_store_free(layout_store);
    rolltui_theme_store_free(theme_store);
    rolltui_layout_free(layout);
    rolltui_bindings_free(bindings);
    rolltui_compose_scratch_free(compose_scratch);
    rolltui_window_stack_free(stack);
    rolltui_windows_free(windows);
    rolltui_u_scratch_free(u_scratch);
    rolltui_draw_scratch_free(draw_scratch);
    rolltui_effect_scratch_free(effect_scratch);
    rolltui_effect_map_free(effects);
    rolltui_context_free(ctx);  // LAST
  }

  RolltuiRect area() const { return {0, 0, w, fh > 0 ? fh : h}; }
  const RolltuiStyle& style(unsigned char role) const { return *rolltui_theme_style(styles, ROLLTUI_ROLE_COUNT, role); }
  RolltuiInput* prompt() { return rolltui_windows_input(windows, "prompt", 6); }
  RolltuiMenu* menu() { return rolltui_windows_menu(windows, "main", 4); }
  RolltuiTranscript* transcript() { return rolltui_windows_transcript(windows, "session", 7); }
  StrView draft() {
    std::size_t n = 0;
    const char* t = rolltui_input_text(prompt(), &n);
    return StrView(t, n);
  }

  // ---- start ---------------------------------------------------------------------------------

  void open_stores(StrView dir) {
    theme_store = rolltui_theme_store_new(ctx, dir.data(), dir.size(), 0, "", 0);
    layout_store = rolltui_layout_store_new(ctx, dir.data(), dir.size(), 0, "", 0);
    keys_store = rolltui_bindings_store_new(ctx, dir.data(), dir.size(), 0, "", 0);
    RolltuiThemePresetReport trep{};
    RolltuiLayoutPresetReport lrep{};
    RolltuiBindingsPresetReport brep{};
    rolltui_theme_store_start(theme_store, &trep);
    rolltui_layout_store_start(layout_store, &lrep);
    rolltui_bindings_store_start(keys_store, &brep);
    // A store that fell back says why, and the conversation is where this app says things.
    if (!trep.error.empty()) say_note(RolltuiStr("theme: ") + trep.error);
    if (!lrep.error.empty()) say_note(RolltuiStr("layout: ") + lrep.error);
    if (!brep.error.empty()) say_note(RolltuiStr("bindings: ") + brep.error);
    rolltui_theme_preset_report_release(&trep);
    rolltui_layout_preset_report_release(&lrep);
    rolltui_bindings_preset_report_release(&brep);
    settings = rolltui_settings_new(theme_store, layout_store, keys_store);
    rolltui_windows_set_theme_store(windows, "theme", 5, theme_store, /*persist=*/1);
    rolltui_windows_set_bindings_store(windows, "keys", 4, keys_store, /*persist=*/1);
  }

  // THE ENVIRONMENT RUNG, applied once and said out loud. A value from the environment fills THIS
  // run's working copy and is never written: the person's saved choice is still there next time.
  void apply_environment() {
    for (std::size_t i = 0; i < rolltui_settings_count(); ++i) {
      const RolltuiSetting* row = rolltui_settings_at(i);
      const RolltuiStr var = RolltuiStr(kEnvPrefix) + StrView(row->env_suffix, row->env_suffix_len);
      const char* env = std::getenv(var.c_str());
      RolltuiStr value;
      RolltuiSettingRung rung{};
      rolltui_settings_resolve(settings, row->key, row->key_len, env ? env : "", env ? std::strlen(env) : 0, &value, &rung);
      if (rung != ROLLTUI_SETTING_RUNG_ENV) continue;
      std::size_t rn = 0;
      const char* rname = rolltui_setting_rung_name(rung, &rn);
      RolltuiSettingsReport rep{};
      const bool ok = rolltui_settings_set(settings, row->key, row->key_len, value.data(), value.size(), /*persist=*/0, &rep) != 0;
      say_note(ok ? rolltui::format("%.*s: '%s' from the %.*s (%s), for this run only", static_cast<int>(row->key_len), row->key,
                                    value.c_str(), static_cast<int>(rn), rname, var.c_str())
                  : rolltui::format("%s: %s", var.c_str(), rep.error.c_str()));
      rolltui_settings_report_release(&rep);
    }
  }

  void mount() {
    rolltui_windows_bind_document(windows, "session", 7, &doc);
    rolltui_windows_bind_submit(windows, "prompt", 6, on_submit, this, nullptr, /*on_submit=*/0);
    // Enter in the find bar goes to the next match and KEEPS the query, so it can be pressed again.
    rolltui_windows_bind_submit(
        windows, "find", 4, [](void* c, const char*, std::size_t) { rolltui_transcript_find_next(static_cast<App*>(c)->transcript()); },
        this, nullptr, /*on_submit=*/1);
    rolltui_windows_bind_note(windows, "prompt", 6, note_fn, this, nullptr);
    rolltui_windows_bind_rows(windows, "status", 6, status_fn, this, nullptr);
    rolltui_context_set_help(ctx, kHelpLead, std::strlen(kHelpLead), kHelpNote, std::strlen(kHelpNote));
    rolltui_context_clear_help_scopes(ctx);
    for (const char* s : kHelpScopes) rolltui_context_add_help_scope(ctx, s, std::strlen(s));
    rolltui_window_stack_set_level_fn(stack, rolltui_windows_back, windows);  // Escape closes one level at a time
    sync_stores();
  }

  // ---- the three working copies, followed by version -----------------------------------------

  void sync_theme() {
    const unsigned long long v = rolltui_theme_store_version(theme_store);
    if (v == theme_seen) return;
    theme_seen = v;
    const RolltuiThemePresetValue* wv = rolltui_theme_store_working(theme_store);
    // "auto" is -1 to both: follow the terminal's own light or dark, and draw at the depth it has.
    const int mode = rolltui_theme_mode_from_name(wv->mode.data(), wv->mode.size());
    if (term) rolltui_terminal_set_depth(term, rolltui_color_depth_from_name(wv->depth.data(), wv->depth.size()));
    RolltuiThemeReport rep{};
    RolltuiStyle got[ROLLTUI_ROLE_COUNT]{};
    RolltuiStr name;
    if (RolltuiEffectMap* eff = rolltui_theme_load(wv->colours, mode, rolltui_theme_vocab(ctx), got, &name, &rep)) {
      std::copy(std::begin(got), std::end(got), styles);
      rolltui_effect_map_free(effects);
      effects = eff;
      RolltuiScrollbarGlyphs g;
      rolltui_theme_scrollbar_glyphs(wv->colours, &g);
      rolltui_context_set_scrollbar_glyphs(ctx, &g);
    } else {
      hint = RolltuiStr("theme: ") + rep.error;
    }
    rolltui_theme_report_release(&rep);
    if (swap) rolltui_swap_invalidate(swap);  // a new palette: repaint whole
  }
  void sync_layout() {
    const unsigned long long v = rolltui_layout_store_version(layout_store);
    if (v == layout_seen) return;
    layout_seen = v;
    rolltui_layout_free(layout);
    layout = rolltui_layout_store_copy_working(layout_store);
    shown = nullptr;  // re-chosen below, at this size
    if (swap) rolltui_swap_invalidate(swap);
  }
  void sync_keys() {
    const unsigned long long v = rolltui_bindings_store_version(keys_store);
    if (v == keys_seen) return;
    keys_seen = v;
    rolltui_bindings_free(bindings);
    bindings = rolltui_bindings_store_copy_working(keys_store);
    declare_actions();
  }
  void declare_actions() {
    if (!bindings || !shown) return;
    std::size_t n = 0;
    const RolltuiLayoutAction* a = rolltui_layout_actions(shown, &n);
    rolltui_bindings_declare(bindings, a, n, kOwnActions, sizeof kOwnActions / sizeof kOwnActions[0]);
  }
  // A TERMINAL SMALLER THAN THE SCREEN NEEDS gets the shipped `stacked` screen instead, which fits
  // in less; the person's own layout comes back when there is room for it again.
  void choose_shown() {
    int mw = 0, mh = 0;
    rolltui_layout_min_size(layout, &mw, &mh);
    const RolltuiLayout* want = layout;
    if (!rolltui_layout_inline(layout) && (w < mw || h < mh))
      if (const RolltuiLayout* narrow = rolltui_layout_shipped(ctx, "stacked", 7)) want = narrow;
    if (want == shown) return;
    shown = want;
    rolltui_window_stack_set_base(stack, rolltui_layout_base(shown));
    declare_actions();
  }
  void sync_stores() {
    sync_theme();
    sync_layout();
    choose_shown();
    sync_keys();
  }

  // ---- the transcript ------------------------------------------------------------------------

  RolltuiDocEntry& add_entry(StrView text, bool markdown, unsigned char role) {
    RolltuiDocEntry e;
    e.id = rolltui::format("m%llu", ++next_id);
    e.text = rolltui::own(text);
    e.markdown = markdown ? 1 : 0;
    e.role = static_cast<rolltui::Role>(role);
    doc.push_back(std::move(e));
    return doc.back();
  }
  // What the app itself says: muted, verbatim, never mistaken for the assistant.
  void say_note(StrView text) { add_entry(text, false, ROLLTUI_ROLE_TEXT_MUTED); }
  void say_block(StrView text) { add_entry(text, false, ROLLTUI_ROLE_TEXT); }

  // ---- a message, and its reply --------------------------------------------------------------

  static void on_submit(void* c, const char* text, std::size_t len) { static_cast<App*>(c)->submit(StrView(text, len)); }

  // WHAT WAS TYPED, kept across runs: Up recalls a message from an earlier session. Empty (headless) keeps nothing.
  RolltuiStr history_path;
  void load_history() {
    if (history_path.empty()) return;
    RolltuiStr err;
    if (!rolltui_input_history_load(rolltui_windows_input(windows, "prompt", 6), history_path.data(), history_path.size(), &err))
      say_note(err);
  }
  void save_history() {
    if (history_path.empty()) return;
    RolltuiStr err;
    if (!rolltui_input_history_save(rolltui_windows_input(windows, "prompt", 6), history_path.data(), history_path.size(), &err))
      rolltui_windows_post(windows, ROLLTUI_MESSAGE_ERROR, err.data(), err.size(), now);
  }

  void submit(StrView text) {
    hint.clear();
    if (text.empty()) return;
    save_history();
    if (text[0] == '/') { command(text); return; }
    if (replying || !approval_cmd.empty()) {
      queued.push_back(rolltui::own(text));
      hint = rolltui::format("queued: it is sent when the reply ends (%zu waiting)", queued.size());
      return;
    }
    send(text);
  }

  void send(StrView text) {
    RolltuiDocEntry& e = add_entry(text, false, ROLLTUI_ROLE_TEXT);
    e.prefix = "\xE2\x80\xBA ";  // ›
    ++you_said;
    words_in += count_words(text);
    if (text[0] == '!' && text.size() > 1) {
      // NOT A REPLY BUT A QUESTION: the assistant wants to run something and waits for a yes. The
      // popup follows this field (`sync_popups`), the way a modal follows the state it asks about.
      approval_cmd = rolltui::own(text.drop_front(1));
      return;
    }
    start_reply(reply_for(text));
  }

  void start_reply(RolltuiStr reply) {
    RolltuiDocEntry& e = add_entry("", true, ROLLTUI_ROLE_TEXT);
    e.state = static_cast<rolltui::EffectState>(ROLLTUI_EFFECT_STATE_WAITING);
    e.state_since_ms = now;
    open_entry = doc.size() - 1;
    open_raw.clear();
    replying = true;
    first_piece = false;
    reply_since = now;
    responder.begin(pieces_of(reply), now);
  }

  // What the other thread produced since the last frame, into the open entry. THE WHOLE RAW TEXT is
  // stripped each time rather than each piece: an escape sequence split across two pieces is only
  // a sequence once both have arrived, and stripping the halves would let its tail through.
  void take_reply() {
    if (!replying) return;
    RolltuiStr got;
    const bool done = responder.drain(got);
    if (got.empty() && !done) return;
    RolltuiDocEntry& e = doc[open_entry];
    if (!got.empty()) {
      open_raw += got;
      e.text = strip_replies ? stripped(open_raw) : rolltui::own(open_raw);
      ++e.version;
      if (!first_piece) {
        first_piece = true;
        e.state = static_cast<rolltui::EffectState>(ROLLTUI_EFFECT_STATE_STREAMING);
        e.state_since_ms = now;
      }
    }
    if (done) finish_reply(false);
  }

  void finish_reply(bool stopped) {
    if (!replying) return;
    RolltuiDocEntry& e = doc[open_entry];
    if (stopped) {
      responder.cancel();
      e.text += e.text.empty() ? "*(stopped)*" : "\n\n*(stopped)*";
      ++e.version;
    }
    // STREAMED, not None: the text has stopped arriving, and a theme that wants to say "this just
    // came from the assistant" has a state to hang that on.
    e.state = static_cast<rolltui::EffectState>(ROLLTUI_EFFECT_STATE_STREAMED);
    e.state_since_ms = now;
    ++assistant_said;
    words_out += count_words(e.text);
    replying = false;
    open_raw.clear();
    if (!queued.empty()) {
      RolltuiStr next = std::move(queued.front());
      queued.erase(queued.begin());
      send(next);
    }
  }

  void answer_approval(bool yes) {
    if (approval_cmd.empty()) return;
    const RolltuiStr cmd = std::move(approval_cmd);
    approval_cmd.clear();
    if (yes && headless) start_reply(rolltui::format("Ran `%s` (pretend: a frame dump runs nothing). It exited 0.", cmd.c_str()));
    else if (yes) run_approved(cmd);
    else say_note(rolltui::format("declined: %s", cmd.c_str()));
    if (!replying && !queued.empty()) {
      RolltuiStr next = std::move(queued.front());
      queued.erase(queued.begin());
      send(next);
    }
  }

  // ---- a command the person approved -----------------------------------------------------------

  // IT RUNS FOR REAL, its output streaming into the conversation as a code block as it arrives, the exit status said at the end.
  // Esc stops it. The output is read as a terminal would show it (colour and cursor moves dropped, a progress line's last state),
  // and the newest 2000 lines are kept.
  RolltuiChild* running = nullptr;
  RolltuiAnsiText* running_out = nullptr;
  RolltuiStr running_id, running_cmd;
  void run_approved(const RolltuiStr& cmd) {
#if defined(_WIN32)
    const char* argv[] = {"cmd", "/d", "/s", "/c", cmd.c_str(), nullptr};
#else
    const char* argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};
#endif
    if (running) return;
    RolltuiChildOptions o{};
    o.argv = argv;
    RolltuiStr err;
    running = rolltui_child_spawn(&o, &err);
    if (!running) {
      rolltui_windows_post(windows, ROLLTUI_MESSAGE_ERROR, err.data(), err.size(), now);
      return;
    }
    running_out = rolltui_ansi_new();
    rolltui_ansi_set_limit(running_out, 2000);
    running_cmd = rolltui::own(cmd);
    running_id = rolltui::own(add_entry(rolltui::format("`$ %s`", cmd.c_str()), true, ROLLTUI_ROLE_TEXT).id);
  }
  void pump_running() {
    if (!running) return;
    rolltui_child_pump(running, running_out, 0);
    int status = -1;
    const bool done = rolltui_child_drained(running) && rolltui_child_done(running, &status);
    RolltuiStr text = rolltui::format("`$ %s`\n\n````text\n", running_cmd.c_str());
    for (std::size_t i = 0, n = rolltui_ansi_line_count(running_out); i < n; ++i) {
      std::size_t len = 0;
      const char* p = rolltui_ansi_line(running_out, i, &len);
      text += StrView(p, len);
      text += "\n";
    }
    text += "````";
    if (done) {
      text += status >= 128 ? rolltui::format("\n\nstopped (signal %d)", status - 128) : rolltui::format("\n\nexit %d", status);
      rolltui_child_free(running);
      running = nullptr;
      rolltui_ansi_free(running_out);
      running_out = nullptr;
    }
    for (std::size_t i = doc.size(); i-- > 0;)
      if (doc[i].id == running_id) {
        if (doc[i].text != text) doc[i].text = std::move(text);
        break;
      }
  }

  // ---- the slash commands --------------------------------------------------------------------

  // THE CONVERSATION FORGOTTEN, a reply still arriving included: it is stopped first, since it arrives into an entry that is about
  // to go, and the messages queued behind it go with it. What an inline screen already left in the terminal's history stays there.
  void cmd_clear() {
    if (replying) {
      responder.cancel();
      replying = false;
      open_raw.clear();
    }
    queued.clear();
    doc.clear();
    history.clear();
    written_before = 0;
    you_said = assistant_said = words_in = words_out = 0;
  }

  void command(StrView line) {
    // "/name rest…"
    std::size_t sp = line.find(' ');
    const StrView name = line.substr(1, sp == StrView::npos ? StrView::npos : sp - 1);
    StrView rest = sp == StrView::npos ? StrView() : line.drop_front(sp + 1);
    while (!rest.empty() && rest[0] == ' ') rest = rest.drop_front(1);
    if (name == "help") cmd_help();
    else if (name == "config") cmd_config();
    else if (name == "set") cmd_set(rest);
    else if (name == "themes") cmd_themes(rest);
    else if (name == "layouts") cmd_layouts(rest);
    else if (name == "keys") cmd_keys(rest);
    else if (name == "terminal") cmd_terminal();
    else if (name == "clear") cmd_clear();
    else if (name == "quit") quit = true;
    else say_note(rolltui::format("unknown command '/%.*s' (/help lists them)", static_cast<int>(name.size()), name.data()));
  }

  // The help popup's document, into the conversation: the same function over the same live table.
  void cmd_help() {
    const char* scopes[3];
    std::size_t lens[3];
    for (std::size_t i = 0; i < 3; ++i) { scopes[i] = kHelpScopes[i]; lens[i] = std::strlen(kHelpScopes[i]); }
    RolltuiStr out;
    rolltui_help_document(bindings, kHelpLead, std::strlen(kHelpLead), scopes, lens, 3, kHelpNote, std::strlen(kHelpNote), &out);
    say_block(out);
  }

  // EACH SETTING AND THE RUNG IT CAME FROM. The resolve is the whole rule (environment, then the
  // working copy, then the built-in); `_get` is what the working copy holds, which an environment
  // value has already filled for this run.
  void cmd_config() {
    RolltuiStr out;
    for (std::size_t i = 0; i < rolltui_settings_count(); ++i) {
      const RolltuiSetting* row = rolltui_settings_at(i);
      const RolltuiStr var = RolltuiStr(kEnvPrefix) + StrView(row->env_suffix, row->env_suffix_len);
      const char* env = std::getenv(var.c_str());
      RolltuiStr value, held, label;
      RolltuiSettingRung rung{};
      rolltui_settings_resolve(settings, row->key, row->key_len, env ? env : "", env ? std::strlen(env) : 0, &value, &rung);
      rolltui_settings_get(settings, row->key, row->key_len, &held);
      rolltui_settings_label(settings, row->key, row->key_len, &label);
      std::size_t rn = 0;
      const char* rname = rolltui_setting_rung_name(rung, &rn);
      rolltui::appendf(out, "%-12.*s %-12s from the %.*s", static_cast<int>(row->key_len), row->key,
                       value.empty() ? "(unset)" : value.c_str(), static_cast<int>(rn), rname);
      if (rung == ROLLTUI_SETTING_RUNG_ENV) rolltui::appendf(out, " (%s)", var.c_str());
      // A field inside a preset names the preset it is edited in; an identity key's label IS its value.
      if (!row->names_preset) rolltui::appendf(out, ", in %.*s '%s'", static_cast<int>(row->store_len), row->store, label.c_str());
      else if (!(label == held)) rolltui::appendf(out, " (%s)", label.c_str());
      rolltui::appendf(out, "\n%-12s one of: %.*s\n", "", static_cast<int>(row->values_len), row->values);
    }
    out += "change one with /set KEY VALUE; a CHAT_<KEY> variable fills one run without changing it";
    say_block(out);
  }

  void cmd_set(StrView rest) {
    const std::size_t sp = rest.find(' ');
    const StrView key = rest.first(sp == StrView::npos ? rest.size() : sp);
    const StrView value = sp == StrView::npos ? StrView() : rest.drop_front(sp + 1);
    const RolltuiSetting* row = rolltui_settings_find(key.data(), key.size());
    if (!row || value.empty()) {
      RolltuiStr keys;
      for (std::size_t i = 0; i < rolltui_settings_count(); ++i) {
        const RolltuiSetting* r = rolltui_settings_at(i);
        if (i) keys += ", ";
        keys += StrView(r->key, r->key_len);
      }
      say_note(row ? rolltui::format("usage: /set %.*s VALUE (%.*s)", static_cast<int>(key.size()), key.data(),
                                     static_cast<int>(row->values_len), row->values)
                   : rolltui::format("unknown setting '%.*s' (%s)", static_cast<int>(key.size()), key.data(), keys.c_str()));
      return;
    }
    RolltuiSettingsReport rep{};
    const bool ok = rolltui_settings_set(settings, key.data(), key.size(), value.data(), value.size(), /*persist=*/1, &rep) != 0;
    RolltuiStr out;
    if (!ok) {
      out = rolltui::own(rep.error);
    } else {
      RolltuiStr label;
      rolltui_settings_label(settings, key.data(), key.size(), &label);
      rolltui::appendf(out, "%.*s = %.*s", static_cast<int>(key.size()), key.data(), static_cast<int>(value.size()), value.data());
      if (!row->names_preset) rolltui::appendf(out, " (in %.*s '%s')", static_cast<int>(row->store_len), row->store, label.c_str());
      if (!rep.problems.empty()) rolltui::appendf(out, " \xE2\x80\x94 %s", rep.problems.c_str());
      for (std::size_t i = 0; i < rep.notes.n; ++i) rolltui::appendf(out, "\n%s", rep.notes.v[i].c_str());
    }
    rolltui_settings_report_release(&rep);
    say_note(out);
  }

  // One line per preset: the one in use marked, "~" when the working copy has drifted from it.
  static void preset_lines(const RolltuiPresetListRaw& list, StrView origin, bool modified, RolltuiStr& out) {
    for (std::size_t i = 0; i < list.n; ++i) {
      const RolltuiPresetInfo& p = list.v[i];
      const bool cur = StrView(p.name) == origin;
      rolltui::appendf(out, "%s %-14s %s%s\n", cur ? (modified ? "~" : "*") : " ", p.name.c_str(),
                       p.shipped ? "shipped" : "yours, ", p.shipped ? "" : p.path.c_str());
    }
  }

  void cmd_themes(StrView name) {
    RolltuiStr out, where;
    if (!name.empty()) {
      // A THEME READ WITHOUT CHOOSING IT: what it carries and where it would load from.
      RolltuiThemePresetReport rep{};
      RolltuiThemePresetValue* v = rolltui_theme_store_get(theme_store, name.data(), name.size(), &rep);
      if (!v) {
        say_note(rep.error);
        rolltui_theme_preset_report_release(&rep);
        return;
      }
      // Where `_load` would look for it, which for a shipped name is where a file of YOURS would
      // shadow it: so whether that file exists is what says which one this is.
      rolltui_theme_store_path(theme_store, name.data(), name.size(), &where);
      const bool shadowed = !where.empty() && ::access(where.c_str(), R_OK) == 0;
      const RolltuiThemePresetValue* shipped = rolltui_theme_shipped(ctx, name.data(), name.size());
      rolltui::appendf(out, "%.*s: %s", static_cast<int>(name.size()), name.data(),
                       shadowed ? (shipped ? "yours, read instead of the shipped one" : "yours") : "shipped with rolltui");
      if (shadowed) rolltui::appendf(out, "\n  file   %s", where.c_str());
      else if (shipped) rolltui::appendf(out, "\n  a file of yours at %s would be read instead", where.c_str());
      rolltui::appendf(out, "\n  saves  mode %s, depth %s", v->mode.empty() ? "auto" : v->mode.c_str(),
                       v->depth.empty() ? "auto" : v->depth.c_str());
      rolltui_theme_preset_value_free(v);
      rolltui_theme_preset_report_release(&rep);
      say_block(out);
      return;
    }
    RolltuiPresetList list;
    rolltui_theme_store_list(theme_store, &list);
    std::size_t on = 0;
    const char* origin = rolltui_theme_store_origin(theme_store, &on);
    preset_lines(list, StrView(origin, on), rolltui_theme_store_modified(theme_store) != 0, out);
    rolltui_theme_store_path(theme_store, nullptr, 0, &where);
    rolltui::appendf(out, "working copy: %s\n/themes NAME reads one without choosing it; /set theme NAME chooses it", where.c_str());
    say_block(out);
  }

  void cmd_layouts(StrView name) {
    RolltuiStr out, where;
    RolltuiPresetList list;
    rolltui_layout_store_list(layout_store, &list);
    std::size_t on = 0;
    const char* origin = rolltui_layout_store_origin(layout_store, &on);
    const bool modified = rolltui_layout_store_modified(layout_store) != 0;
    for (const RolltuiPresetInfo& p : list) {
      if (!name.empty() && !(StrView(p.name) == name)) continue;
      // Each screen READ, not switched to, for the one fact a person choosing between them wants.
      RolltuiLayoutPresetReport rep{};
      RolltuiLayout* l = rolltui_layout_store_get(layout_store, p.name.data(), p.name.size(), &rep);
      int mw = 0, mh = 0;
      if (l) rolltui_layout_min_size(l, &mw, &mh);
      const bool cur = StrView(p.name) == StrView(origin, on);
      rolltui::appendf(out, "%s %-12s %s %s\n", cur ? (modified ? "~" : "*") : " ", p.name.c_str(),
                       !l ? rep.error.c_str() : mw || mh ? rolltui::format("fits %dx%d or larger;", mw, mh).c_str() : "fits any size;",
                       p.shipped ? "shipped" : p.path.c_str());
      rolltui_layout_free(l);
      rolltui_layout_preset_report_release(&rep);
    }
    if (out.empty()) {
      say_note(rolltui::format("no layout '%.*s' (/layouts lists them)", static_cast<int>(name.size()), name.data()));
      return;
    }
    rolltui_layout_store_path(layout_store, nullptr, 0, &where);
    rolltui::appendf(out, "working copy: %s", where.c_str());
    say_block(out);
  }

  void cmd_keys(StrView scope) {
    RolltuiStr out;
    if (!scope.empty()) {
      // ONE SCOPE'S ROWS, from the live table: what a key does in this screen right now.
      rolltui_help_scope_lines(bindings, scope.data(), scope.size(), nullptr, nullptr, 0, "  ", 2, &out);
      if (out.empty()) out = rolltui::format("no keys in a scope named '%.*s'", static_cast<int>(scope.size()), scope.data());
      say_block(out);
      return;
    }
    RolltuiPresetList list;
    rolltui_bindings_store_list(keys_store, &list);
    std::size_t on = 0;
    const char* origin = rolltui_bindings_store_origin(keys_store, &on);
    preset_lines(list, StrView(origin, on), rolltui_bindings_store_modified(keys_store) != 0, out);
    // HAS THE TABLE DRIFTED from what ships under its name? Both dumped the same way and compared.
    if (rolltui_bindings_is_shipped(ctx, origin, on)) {
      RolltuiStr mine, theirs;
      rolltui_bindings_dump_json(rolltui_bindings_store_working(keys_store), origin, on, &mine);
      rolltui_bindings_dump_json(rolltui_bindings_shipped(ctx, origin, on), origin, on, &theirs);
      rolltui::appendf(out, "the working table %s the shipped '%.*s'\n", mine == theirs ? "matches" : "differs from",
                       static_cast<int>(on), origin);
    }
    RolltuiStr where;
    rolltui_bindings_store_path(keys_store, nullptr, 0, &where);
    rolltui::appendf(out, "working copy: %s\n/keys SCOPE lists one scope's keys (app, input, transcript, menu, stack)", where.c_str());
    say_block(out);
  }

  // WHAT WAS REMEMBERED ABOUT THIS TERMINAL, AND WHAT IT ANSWERS NOW. The library asked at entry (or
  // remembered); this asks again, live, and says whether the two agree — the question a person has
  // when the colours look wrong.
  void cmd_terminal() {
    if (!term) { say_note("no terminal: this run draws headless (the self-test's --frame)"); return; }
    RolltuiTermFacts f;
    rolltui_terminal_facts(term, &f);
    RolltuiStr out;
    rolltui::appendf(out, "terminal   %s\n", f.name[0] ? f.name : "(it did not say)");
    rolltui::appendf(out, "depth      %s, from %s\n", name_of(kDepthNames, f.depth), name_of(kFactSources, f.depth_source));
    rolltui::appendf(out, "mode       %s, from %s", name_of(kModeNames, f.mode), name_of(kFactSources, f.mode_source));
    if (f.has_background) {
      char hex[ROLLTUI_COLOR_STRING_MAX];
      const std::size_t n = rolltui_color_to_string(f.background, hex, sizeof hex);
      rolltui::appendf(out, " (background %.*s)", static_cast<int>(n), hex);
    }
    rolltui::appendf(out, "\nambiguous  %d cell%s, from %s\n", f.ambiguous_wide ? 2 : 1, f.ambiguous_wide ? "s" : "",
                     name_of(kFactSources, f.ambiguous_source));
    rolltui::appendf(out, "keys       %s\n", name_of(kProtocolNames, f.keyboard));
    const char* cache = rolltui_terminal_cache_path(term);
    rolltui::appendf(out, "answers    %s%s\n", f.remembered ? "remembered, in " : (f.responsive ? "asked this run, kept in " : "none: it answers nothing"),
                     f.responsive || f.remembered ? (cache && *cache ? cache : "(not kept)") : "");
    RolltuiStyleColorRaw bg{};
    if (rolltui_terminal_query_background(term, 200, &bg)) {
      char hex[ROLLTUI_COLOR_STRING_MAX];
      const std::size_t n = rolltui_color_to_string(bg, hex, sizeof hex);
      const unsigned char mode = rolltui_mode_for_background(bg);
      rolltui::appendf(out, "now: background %.*s, %s (%s)\n", static_cast<int>(n), hex, name_of(kModeNames, mode),
                       mode == f.mode ? "agrees" : "DIFFERS");
    } else {
      out += "now: no answer about the background\n";
    }
    int wide = -1;
    if (rolltui_terminal_query_ambiguous_wide(term, 200, &wide))
      rolltui::appendf(out, "now: ambiguous glyphs %d cell%s (%s)", wide ? 2 : 1, wide ? "s" : "",
                       (wide != 0) == (f.ambiguous_wide != 0) ? "agrees" : "DIFFERS");
    else
      out += "now: no answer about ambiguous glyphs";
    // The width question draws and erases a glyph at the top left: what is there is repainted.
    if (swap) rolltui_swap_invalidate(swap);
    say_block(out);
  }

  // ---- the menu and the palette --------------------------------------------------------------

  void close_popup(StrView id) {
    // Pop down to the named layer, then it: what was opened over it goes with it.
    while (rolltui_window_stack_depth(stack) > 1) {
      const RolltuiLayer* top = rolltui_window_stack_layer(stack, rolltui_window_stack_depth(stack) - 1);
      std::size_t n = 0;
      const char* tid = rolltui_layer_id(top, &n);
      const bool it = StrView(tid, n) == id;
      rolltui_window_stack_pop(stack);
      if (it) break;
    }
  }
  bool popup_open(const char* id) const { return rolltui_window_stack_has_popup(stack, id, std::strlen(id)) != 0; }
  // A popup this screen does not declare is taken from the shipped `default`, which declares them all.
  void push_popup(const char* id) {
    if (!rolltui_window_stack_push_popup(stack, shown, id, std::strlen(id)))
      rolltui_window_stack_push_popup(stack, rolltui_layout_shipped(ctx, "default", 7), id, std::strlen(id));
  }

  void fill_choice(const char* key, const RolltuiPresetListRaw& list) {
    RolltuiMenuItemList options;
    for (std::size_t i = 0; i < list.n; ++i) {
      const RolltuiPresetInfo& p = list.v[i];
      const RolltuiStr label = p.shipped ? rolltui::own(p.name) : p.name + "  (yours)";
      options.push_back(RolltuiMenuItem::action(p.name.c_str(), label.c_str()));
    }
    const RolltuiStr id = RolltuiStr("settings.") + key;
    rolltui_menu_set_options(menu(), id.data(), id.size(), &options);
  }
  void show_setting_values() {
    for (std::size_t i = 0; i < rolltui_settings_count(); ++i) {
      const RolltuiSetting* row = rolltui_settings_at(i);
      RolltuiStr v;
      rolltui_settings_get(settings, row->key, row->key_len, &v);
      const RolltuiStr id = RolltuiStr("settings.") + StrView(row->key, row->key_len);
      rolltui_menu_set_value(menu(), id.data(), id.size(), v.empty() ? "auto" : v.data(), v.empty() ? 4 : v.size());
    }
  }
  void open_menu(bool palette) {
    if (popup_open("menu")) { close_popup("menu"); return; }
    // THE SKELETON IS THE FILE; what exists right now is filled in here.
    RolltuiPresetList themes, layouts, keys;
    rolltui_theme_store_list(theme_store, &themes);
    rolltui_layout_store_list(layout_store, &layouts);
    rolltui_bindings_store_list(keys_store, &keys);
    fill_choice("theme", themes);
    fill_choice("layout", layouts);
    fill_choice("bindings", keys);
    show_setting_values();
    rolltui_menu_apply_shortcuts(rolltui_menu_root(menu()), bindings);
    rolltui_menu_reset(menu());
    rolltui_menu_set_palette(menu(), palette);
    push_popup("menu");
  }

  void menu_event(RolltuiMenuEvent& ev) {
    const StrView id = ev.id;
    if (ev.kind == ROLLTUI_MENU_EVENT_CLOSED) { close_popup("menu"); return; }
    if (ev.kind == ROLLTUI_MENU_EVENT_CHOOSE && id.starts_with("settings.")) {
      // ONE BRANCH FOR ALL FIVE: which working copy a key lives in is the settings table's answer.
      const StrView key = id.drop_front(9);
      RolltuiSettingsReport rep{};
      if (rolltui_settings_set(settings, key.data(), key.size(), ev.value.data(), ev.value.size(), /*persist=*/1, &rep)) {
        RolltuiStr label;
        rolltui_settings_label(settings, key.data(), key.size(), &label);
        hint = rolltui::format("%.*s: %s", static_cast<int>(key.size()), key.data(), label.c_str());
      } else {
        hint = rolltui::own(rep.error);
      }
      rolltui_settings_report_release(&rep);
      show_setting_values();
      return;
    }
    if (ev.kind != ROLLTUI_MENU_EVENT_ACTIVATE || !id.starts_with("cmd.")) return;
    // Copied before the menu closes: what the item says is the menu's, and the menu is going.
    const RolltuiMenuItem* it = rolltui_menu_find(menu(), id.data(), id.size());
    const RolltuiStr action = it ? rolltui::own(it->action_name) : RolltuiStr();
    const RolltuiStr name = rolltui::own(id.drop_front(4));
    close_popup("menu");
    // AN ITEM THAT DOES WHAT A KEY DOES is that key: the same popup, the same editor.
    if (action == "app.editor") { editor_wanted = true; return; }
    if (action == "app.find") { open_find(); return; }
    if (action == "app.details") { open_details(); return; }
    if (!action.empty() && rolltui_window_stack_action_popup(stack, shown, action.data(), action.size())) return;
    // Everything else is a slash command, exactly as if typed: one dispatcher.
    command(RolltuiStr("/") + name);
  }

  // The focused item's description, while the menu is open: what the palette row will DO.
  RolltuiStr focused_about() {
    if (!popup_open("menu")) return RolltuiStr();
    RolltuiStr fid;
    rolltui_menu_focused(menu(), &fid);
    const RolltuiMenuItem* it = fid.empty() ? nullptr : rolltui_menu_find(menu(), fid.data(), fid.size());
    return it ? rolltui::own(it->description) : RolltuiStr();
  }

  // ---- the other popups ----------------------------------------------------------------------

  void open_find() {
    if (popup_open("find")) {
      close_popup("find");
      rolltui_input_clear(rolltui_windows_input(windows, "find", 4));
      rolltui_transcript_set_query(transcript(), "", 0);
      return;
    }
    push_popup("find");
    rolltui_window_stack_focus(stack, "find", 4);
  }
  void sync_find() {
    if (!popup_open("find")) return;
    std::size_t n = 0;
    const char* q = rolltui_input_text(rolltui_windows_input(windows, "find", 4), &n);
    rolltui_transcript_set_query(transcript(), q, n);
  }
  void open_details() {
    if (popup_open("details")) { close_popup("details"); return; }
    details_top = 0;
    push_popup("details");
  }
  // The approval popup FOLLOWS THE QUESTION: up while one is open, gone once it is answered.
  void sync_popups() {
    const bool asking = !approval_cmd.empty();
    if (asking && !popup_open("approval")) push_popup("approval");
    if (!asking && popup_open("approval")) close_popup("approval");
  }
  void take_picked_file() {
    RolltuiStr got;
    if (rolltui_windows_picker_taken(windows, "filepicker", 10, &got) && !got.empty()) {
      // A FILE CHOSEN IS A PATH IN THE MESSAGE, where a person can see what they are about to send.
      RolltuiStr text = rolltui::own(draft());
      if (!text.empty() && text.data()[text.size() - 1] != ' ') text += ' ';
      text += got;
      rolltui_input_set_text(prompt(), text.data(), text.size());
      close_popup("filepicker");
    }
  }

  // ---- the frame -----------------------------------------------------------------------------

  // THE PLACEHOLDER SAYS WHAT ENTER WILL DO NOW, and it is set only when it changes: an input's
  // options are compared before they are replaced, so an unchanged frame changes nothing.
  void sync_prompt_options() {
    RolltuiInputOptions o{};
    rolltui_input_options_copy(&o, rolltui_input_options(prompt()));
    o.placeholder = replying ? "replying: Enter queues a message, Esc stops the reply"
                             : "Message the pretend assistant (/help lists the commands)";
    if (!rolltui_input_options_equal(&o, rolltui_input_options(prompt()))) rolltui_input_set_options(prompt(), &o);
    rolltui_input_options_release(&o);
    // THE QUESTION SITS OVER THE PROMPT, so the prompt is made as tall as the question's box: the
    // transcript keeps every line it had, and only the prompt is covered.
    int min_outer = 0;
    if (!approval_cmd.empty())
      if (const RolltuiLayer* q = rolltui_layout_popup(shown, "approval", 8)) {
        RolltuiRect r{};
        rolltui_placement_resolve(rolltui_layer_placement(q), area(), &r);
        min_outer = r.h;
      }
    rolltui_windows_set_input_min_outer(windows, "prompt", 6, min_outer);
  }

  RolltuiStr window_note;
  void prepare() {
    pump_running();
    sync_stores();
    choose_shown();
    take_picked_file();
    sync_popups();
    sync_find();
    sync_prompt_options();
    // A FRAME DUMP IS ONE MOMENT: nothing the library moves by itself is caught halfway (`still`).
    const RolltuiWidgetEnv env{ambiguous, now, static_cast<unsigned char>(headless)};
    rolltui_context_set_env(ctx, &env);
    rolltui_context_set_bindings(ctx, bindings);
    rolltui_windows_sync(windows, stack);
    rolltui_windows_autosize(windows, stack, area());
    rolltui_windows_layout(windows, stack, area());
    window_note.clear();
    if (rolltui_windows_report_count(windows) != 0) rolltui_windows_report_summary(windows, &window_note);
  }

  void render_into(RolltuiFrame* f) {
    prepare();
    rolltui_window_stack_compose(stack, f, area(), styles, rolltui_layout_default_roles(), draw_slot, this, ambiguous,
                                 compose_scratch);
    toast_wait = rolltui_windows_draw_toasts(windows, f, area(), styles, now);
    if (rolltui_frame_mark_count(f) != 0 && effects && !rolltui_effect_map_empty(effects)) {
      RolltuiEffectReport rep{};
      rolltui_effects_apply(ctx, f, effect_scratch, styles, nullptr, effects, now, 0, &rep, nullptr, nullptr);
    }
  }
  static void draw_slot(void* c, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
    App& a = *static_cast<App*>(c);
    rolltui_windows_draw(a.windows, rn, f, a.styles, rolltui_windows_default_roles());
  }
  // NOTHING ON SCREEN MOVES unless the frame marked something that does, so an idle loop sleeps
  // until a key, a resize or the other thread's wake.
  int toast_wait = -1;  // when the toasts next change, from the last frame
  int poll_timeout_ms(const RolltuiFrame* f) {
    int t = rolltui_transcript_wants_tick(transcript()) ? 50 : -1;
    if (toast_wait >= 0) t = t < 0 ? toast_wait : std::min(t, toast_wait);
    if (const int r = rolltui_child_wait_ms(running); running && r >= 0) t = t < 0 ? r : std::min(t, r);
    if (f && rolltui_frame_mark_count(f) != 0 && effects && !rolltui_effect_map_empty(effects)) {
      const int tick = rolltui_effects_tick_ms(ctx, f, effects);
      if (tick > 0) t = t < 0 ? tick : std::min(t, tick);
    }
    return t;
  }

  // The window's title says what the conversation is doing. A reply cannot set it — its escapes are
  // stripped — so what it says is only ever this.
  RolltuiStr title() const {
    if (!approval_cmd.empty()) return RolltuiStr("rolltui-chat \xC2\xB7 waiting for you");
    if (replying) return RolltuiStr("rolltui-chat \xC2\xB7 replying\xE2\x80\xA6");
    return rolltui::format("rolltui-chat \xC2\xB7 %zu message%s", you_said + assistant_said, you_said + assistant_said == 1 ? "" : "s");
  }
  void write_title() {
    const RolltuiStr t = title();
    rolltui_terminal_set_title(term, t.data(), t.size());  // only a change is written; the terminal's own title comes back on leaving
  }


  // ---- the sources this app binds ------------------------------------------------------------

  static void note_fn(void* c, RolltuiNoteRaw* out) {
    App& a = *static_cast<App*>(c);
    if (!a.approval_cmd.empty()) { out->text = "answer the question above: y or n"; return; }
    if (!a.hint.empty()) { out->text = rolltui::own(a.hint); return; }
    if (a.replying && !a.first_piece) { out->text = "thinking\xE2\x80\xA6 (Esc stops it)"; return; }
    if (a.replying) {
      out->text = "replying\xE2\x80\xA6 (Esc stops it)";
      out->state = static_cast<rolltui::EffectState>(ROLLTUI_EFFECT_STATE_STREAMING);
      out->since_ms = a.reply_since;
      return;
    }
    out->text = "Enter sends \xC2\xB7 F1 help \xC2\xB7 F2 menu \xC2\xB7 Ctrl-P commands";
  }

  static void status_fn(void* c, RolltuiRows* out) {
    App& a = *static_cast<App*>(c);
    // A WINDOW'S OWN REPORT outranks everything: the status panel is truncated from the bottom.
    if (!a.window_note.empty()) rolltui_rows_add(out, "", 0, a.window_note.data(), a.window_note.size());
    const RolltuiStr about = a.focused_about();
    if (!about.empty()) rolltui_rows_add(out, "about", 5, about.data(), about.size());
    const RolltuiStr msgs = rolltui::format("%zu you, %zu assistant", a.you_said, a.assistant_said);
    rolltui_rows_add(out, "messages", 8, msgs.data(), msgs.size());
    const char* state = !a.approval_cmd.empty() ? "waiting for you" : a.replying ? "replying" : "idle";
    rolltui_rows_add(out, "state", 5, state, std::strlen(state));
    if (!a.queued.empty()) {
      const RolltuiStr q = rolltui::to_str(static_cast<long long>(a.queued.size()));
      rolltui_rows_add(out, "queued", 6, q.data(), q.size());
    }
    RolltuiStr label;
    rolltui_theme_store_label(a.theme_store, &label);
    rolltui_rows_add(out, "theme", 5, label.data(), label.size());
    rolltui_layout_store_label(a.layout_store, &label);
    if (a.shown != a.layout) label += " (too small: stacked)";
    rolltui_rows_add(out, "layout", 6, label.data(), label.size());
    rolltui_bindings_store_label(a.keys_store, &label);
    rolltui_rows_add(out, "keys", 4, label.data(), label.size());
  }

  // ---- the approval kind: the assistant's question -------------------------------------------
  // Its ctx IS the app, and `destroy` frees nothing: the app outlives the window table.
  static void approval_destroy(void*) {}
  static void approval_layout(void*, const RolltuiResolvedNode*) {}
  static void approval_draw(void* c, const RolltuiResolvedNode* rn, RolltuiFrame* f) {
    App& a = *static_cast<App*>(c);
    RolltuiRect r{};
    rolltui_content_rect(rn, &r);
    const RolltuiStr cmd = RolltuiStr("  $ ") + a.approval_cmd;
    const StrView lines[3] = {"The assistant wants to run a command (pretend: nothing runs).", cmd,
                              "y runs it \xC2\xB7 n, Enter or Esc declines"};
    const unsigned char roles[3] = {ROLLTUI_ROLE_WARNING, ROLLTUI_ROLE_TEXT, ROLLTUI_ROLE_PROMPT};
    int y = r.y, end_x = r.x;
    for (int i = 0; i < 3 && y < r.y + r.h; ++i, ++y)
      end_x = r.x + rolltui_frame_put_text(f, a.draw_scratch, r.x, y, lines[i].data(), lines[i].size(), a.style(roles[i]),
                                           std::max(r.w, 0), a.ambiguous, 0);
    // The cursor waits where the answer would be typed, so a person sees the question is for them.
    if (rn->focused && r.h > 0 && r.w > 0)
      rolltui_frame_set_cursor(f, std::min(end_x + 1, r.x + r.w - 1), std::min(y - 1, r.y + r.h - 1), 1);
  }
  static int approval_handle(void* c, const RolltuiEvent* e) {
    App& a = *static_cast<App*>(c);
    if (e->kind != ROLLTUI_EVENT_KEY) return 1;  // a modal question takes everything
    const RolltuiChord& k = e->key;
    const bool plain = k.key == ROLLTUI_KEY_CHAR && !k.ctrl && !k.alt;
    if (plain && (k.ch == 'y' || k.ch == 'Y')) a.answer_approval(true);
    else if (k.key == ROLLTUI_KEY_ENTER || (plain && (k.ch == 'n' || k.ch == 'N'))) a.answer_approval(false);
    return 1;
  }
  static constexpr RolltuiWidgetPlugin kApprovalPlugin = {
      approval_destroy, approval_layout, approval_draw, nullptr, nullptr, nullptr, approval_handle, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
  static RolltuiWidget approval_factory(void* c, RolltuiWindows*, const char*, std::size_t) { return RolltuiWidget{&kApprovalPlugin, c}; }

  // ---- the details kind: the session report --------------------------------------------------

  std::vector<RolltuiStr> details_lines() {
    std::vector<RolltuiStr> l;
    l.push_back("conversation");
    l.push_back(rolltui::format("  messages   %zu from you, %zu from the assistant", you_said, assistant_said));
    l.push_back(rolltui::format("  words      %zu typed, %zu received", words_in, words_out));
    l.push_back(rolltui::format("  now        %s", !approval_cmd.empty() ? "waiting for your answer" : replying ? "replying" : "idle"));
    l.push_back("");
    l.push_back("look");
    RolltuiStr label, where;
    rolltui_theme_store_label(theme_store, &label);
    rolltui_theme_store_path(theme_store, nullptr, 0, &where);
    l.push_back(rolltui::format("  theme      %s", label.c_str()));
    l.push_back(rolltui::format("             %s", where.c_str()));
    rolltui_layout_store_label(layout_store, &label);
    rolltui_layout_store_path(layout_store, nullptr, 0, &where);
    l.push_back(rolltui::format("  layout     %s", label.c_str()));
    l.push_back(rolltui::format("             %s", where.c_str()));
    rolltui_bindings_store_label(keys_store, &label);
    rolltui_bindings_store_path(keys_store, nullptr, 0, &where);
    l.push_back(rolltui::format("  keys       %s", label.c_str()));
    l.push_back(rolltui::format("             %s", where.c_str()));
    l.push_back("");
    l.push_back("terminal");
    if (term) {
      RolltuiTermFacts f;
      rolltui_terminal_facts(term, &f);
      l.push_back(rolltui::format("  name       %s", f.name[0] ? f.name : "(it did not say)"));
      l.push_back(rolltui::format("  depth      %s", name_of(kDepthNames, f.depth)));
      l.push_back(rolltui::format("  size       %dx%d", w, h));
    } else {
      l.push_back(rolltui::format("  none: drawn headless at %dx%d", w, h));
    }
    return l;
  }

  void draw_details(const RolltuiResolvedNode& rn, RolltuiFrame* f) {
    RolltuiRect r{};
    rolltui_content_rect(&rn, &r);
    const std::vector<RolltuiStr> lines = details_lines();
    const int total = static_cast<int>(lines.size());
    details_top = std::max(0, std::min(details_top, total - std::max(r.h, 1)));
    for (int row = 0; row < r.h && details_top + row < total; ++row) {
      const RolltuiStr& s = lines[static_cast<std::size_t>(details_top + row)];
      const bool heading = !s.empty() && s.data()[0] != ' ';
      const RolltuiStyle st = style(heading ? ROLLTUI_ROLE_TITLE : ROLLTUI_ROLE_TEXT);
      // A LINE TOO LONG FOR THE PANEL ENDS IN AN ELLIPSIS, cut where the drawing would cut it — never
      // half a wide glyph — so a long path still says it was cut.
      std::size_t keep = rolltui_u_fit(u_scratch, s.data(), s.size(), r.w, ambiguous, nullptr);
      if (keep < s.size() && r.w > 1) {
        int cells = 0;
        keep = rolltui_u_fit(u_scratch, s.data(), s.size(), r.w - 1, ambiguous, &cells);
        rolltui_frame_put_text(f, draw_scratch, r.x, r.y + row, s.data(), keep, st, r.w - 1, ambiguous, 0);
        rolltui_frame_put_text(f, draw_scratch, r.x + cells, r.y + row, "\xE2\x80\xA6", 3, style(ROLLTUI_ROLE_TEXT_MUTED), 1,
                               ambiguous, 0);
      } else {
        rolltui_frame_put_text(f, draw_scratch, r.x, r.y + row, s.data(), keep, st, r.w, ambiguous, 0);
      }
    }
  }
  // A page is the details window's own height this frame, asked of the stack rather than guessed.
  void details_key(const RolltuiChord& k) {
    struct Page { int rows = 10; } page;
    rolltui_window_stack_resolve(
        stack, area(),
        [](void* c, const RolltuiResolvedNode* rn) {
          std::size_t n = 0;
          const char* id = rolltui_layout_node_is_window(rn->node) ? rolltui_layout_node_id(rn->node, &n) : "";
          if (StrView(id, n) == "details") {
            RolltuiRect inner{};
            rolltui_content_rect(rn, &inner);
            static_cast<Page*>(c)->rows = std::max(inner.h, 1);
          }
        },
        &page);
    // The line count NOW, not the one the last draw saw: a key can arrive before the panel ever drew.
    const int total = static_cast<int>(details_lines().size());
    rolltui_scroll_by_action(rolltui_scroll_text_default_actions(), bindings, &k, page.rows, total, &details_top);
  }
  static void details_destroy(void*) {}
  static void details_layout(void*, const RolltuiResolvedNode*) {}
  static void details_draw(void* c, const RolltuiResolvedNode* rn, RolltuiFrame* f) { static_cast<App*>(c)->draw_details(*rn, f); }
  static int details_handle(void* c, const RolltuiEvent* e) {
    App& a = *static_cast<App*>(c);
    if (e->kind == ROLLTUI_EVENT_KEY) {
      a.details_key(e->key);
      return 1;
    }
    if (e->kind == ROLLTUI_EVENT_MOUSE && (e->mouse.kind == RolltuiMouseEvent::Kind::WheelUp ||
                                           e->mouse.kind == RolltuiMouseEvent::Kind::WheelDown)) {
      a.details_top += e->mouse.kind == RolltuiMouseEvent::Kind::WheelUp ? -3 : 3;
      a.details_top = std::max(0, a.details_top);
      return 1;
    }
    return 0;
  }
  static constexpr RolltuiWidgetPlugin kDetailsPlugin = {
      details_destroy, details_layout, details_draw, nullptr, nullptr, nullptr, details_handle, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
  static RolltuiWidget details_factory(void* c, RolltuiWindows*, const char*, std::size_t) { return RolltuiWidget{&kDetailsPlugin, c}; }

  // ---- input ---------------------------------------------------------------------------------

  void handle(const RolltuiEvent& in) {
    RolltuiEvent e = in;
    if (rolltui_windows_toast_event(windows, &e)) return;  // a press on a toast dismisses it, and nothing under it
    // A PASTE IS TEXT FROM SOMEWHERE ELSE, and is stripped like a reply before it reaches the prompt.
    RolltuiStr clean;
    if (e.kind == ROLLTUI_EVENT_PASTE && e.text) {
      clean = stripped(StrView(e.text, e.text_len));
      e.text = clean.data();
      e.text_len = clean.size();
    }
    if (e.kind == ROLLTUI_EVENT_KEY && approval_cmd.empty()) {
      std::size_t alen = 0;
      const char* ap = rolltui_bindings_action_for(bindings, &e.key, "app", 3, &alen);
      const StrView act(ap ? ap : "", alen);
      const bool popup = rolltui_window_stack_depth(stack) > 1;
      // A PRINTABLE CHORD IS TYPING while there is a draft or anything open: "?" opens help only on
      // an empty line with nothing over it.
      const bool printable = e.key.key == ROLLTUI_KEY_CHAR && !e.key.ctrl && !e.key.alt;
      const bool typing = printable && (!draft().empty() || popup);
      if (act == "app.repaint") { if (swap) rolltui_swap_invalidate(swap); return; }
      if (act == "app.stop" && running && !popup) { rolltui_child_stop(running); return; }
      if (act == "app.stop" && replying && !popup) { finish_reply(true); return; }
      if (!typing) {
        if (act == "app.editor" && !popup) { editor_wanted = true; return; }
        if (act == "app.menu") { open_menu(false); return; }
        if (act == "app.palette") { open_menu(true); return; }
        if (act == "app.find") { open_find(); return; }
        if (act == "app.details") { open_details(); return; }
        // ANY OTHER `app.<id>` NAMING A POPUP THIS SCREEN DECLARES opens it: help, the theme and keys
        // editors, the file picker. A panel is a layout entry and a chord, and no code here.
        if (!act.empty() && rolltui_window_stack_action_popup(stack, shown, act.data(), act.size())) return;
      }
    }
    RolltuiStr window;
    const unsigned char kind = rolltui_window_stack_route(stack, &e, area(), bindings, rolltui_stack_default_actions(), &window);
    if (kind == ROLLTUI_ROUTE_CLOSED_POPUP) {
      // The question closed by its close key is a no; the find bar closed takes its highlight with it.
      if (window == "approval") answer_approval(false);
      if (window == "find") {
        rolltui_input_clear(rolltui_windows_input(windows, "find", 4));
        rolltui_transcript_set_query(transcript(), "", 0);
      }
      return;
    }
    if (kind != ROLLTUI_ROUTE_DELIVER) return;
    std::size_t cn = 0;
    const char* content = rolltui_windows_content_at(windows, window.data(), window.size(), &cn);
    if (StrView(content ? content : "", cn) == "input:prompt") {
      // THE PROMPT FIRST; what it ignores (a page key) is the transcript's.
      switch (rolltui_input_kind_process_event(prompt(), windows, "prompt", 6, &e)) {
        case ROLLTUI_INPUT_EOF: if (draft().empty()) quit = true; return;
        case ROLLTUI_INPUT_IGNORED: break;
        default: return;
      }
      rolltui_transcript_handle(transcript(), &e, &doc, now, bindings, rolltui_transcript_default_actions());
      return;
    }
    if (RolltuiMenu* m = rolltui_windows_menu_at(windows, window.data(), window.size())) {
      RolltuiMenuEvent ev{};
      rolltui_menu_handle(m, &e, bindings, rolltui_menu_default_actions(), &ev);
      menu_event(ev);
      rolltui_menu_event_release(&ev);
      return;
    }
    rolltui_windows_handle(windows, window.data(), window.size(), &e);  // the transcript, help, the editors, the picker, ours
  }

  // ---- the editor: the terminal handed over and taken back -----------------------------------

  void run_editor() {
    editor_wanted = false;
    if (!term) { hint = "no terminal to hand to an editor"; return; }
    const StrView d = draft();
#if defined(_WIN32)
    static unsigned drafts = 0;
    const char* tmp = std::getenv("TEMP");
    RolltuiStr path = rolltui::format("%s\\rolltui-chat-%d-%u.md", tmp && *tmp ? tmp : ".", _getpid(), ++drafts);
    FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) { hint = "could not make a file for the editor"; return; }
    const bool wrote = std::fwrite(d.data(), 1, d.size(), out) == d.size();
    std::fclose(out);
    const char* fallback = "notepad";
    const char quote = '"';
#else
    const char* tmp = std::getenv("TMPDIR");
    RolltuiStr path = rolltui::format("%s/rolltui-chat-XXXXXX.md", tmp && *tmp ? tmp : "/tmp");
    const int fd = mkstemps(path.p, 3);
    if (fd < 0) { hint = "could not make a file for the editor"; return; }
    const bool wrote = ::write(fd, d.data(), d.size()) == static_cast<ssize_t>(d.size());
    ::close(fd);
    const char* fallback = "vi";
    const char quote = '\'';
#endif
    const char* ed = std::getenv("VISUAL");
    if (!ed || !*ed) ed = std::getenv("EDITOR");
    if (!ed || !*ed) ed = fallback;
    if (wrote && !StrView(path).contains(quote)) {
      const RolltuiStr cmd = rolltui::format("%s %c%s%c", ed, quote, path.c_str(), quote);
      // THE TERMINAL IS THE EDITOR'S until it exits: its own modes, the main screen, no raw input.
      rolltui_terminal_suspend(term);
      const int rc = std::system(cmd.c_str());
      rolltui_terminal_resume(term);
      rolltui_swap_invalidate(swap);  // what was on the screen is gone
      RolltuiStr text;
      if (FILE* fp = std::fopen(path.c_str(), "rb")) {
        char buf[4096];
        for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, fp)) > 0;) text.append(buf, n);
        std::fclose(fp);
      }
      while (!text.empty() && (text.data()[text.size() - 1] == '\n' || text.data()[text.size() - 1] == '\r')) rolltui::pop_back(text);
      if (rc == 0) rolltui_input_set_text(prompt(), text.data(), text.size());
      else hint = rolltui::format("the editor exited with %d; the message is as it was", rc);
    }
    std::remove(path.c_str());
  }

  // ---- inline: the region, and what leaves it -------------------------------------------------

  // THE TERMINAL MADE TO THE LAYOUT: inline below the prompt, or the whole screen.
  void open_terminal() {
    RolltuiTerminalOptions opts{};
    opts.facts_events = 1;
    opts.hover = 1;  // what a click would act on is lit under the pointer
    if (inline_mode) opts.inline_rows = 3;  // the height is the frame's: `frame_rows`
    term = rolltui_terminal_new(STDIN_FILENO, STDOUT_FILENO, opts);
    read_facts();
    w = rolltui_terminal_width(term);
    h = rolltui_terminal_height(term);
    fh = 0;
  }
  // A LAYOUT THAT CHANGES WHERE THE SCREEN IS DRAWN changes it at once: the terminal is left (the alternate screen, or the region)
  // and made again for the other. What was said so far is committed into the terminal's history by the next frame when the screen
  // becomes inline; the whole screen shows what has not been.
  void switch_screen() {
    responder.retarget(nullptr);
    rolltui_terminal_free(term);
    if (inline_mode && history.size() > 0) {  // the whole screen shows the conversation from its start
      RolltuiDocument all;
      for (const RolltuiDocEntry& e : history) rolltui_doc_entry_copy(rolltui_document_add(&all), &e);
      for (const RolltuiDocEntry& e : doc) rolltui_doc_entry_copy(rolltui_document_add(&all), &e);
      if (replying) open_entry += history.size();
      written_before = history.size();
      doc = std::move(all);
      history.clear();
    } else if (!inline_mode) {  // what the terminal's history already holds goes back to `history` unwritten
      for (; written_before > 0 && doc.size() > 0; --written_before) {
        rolltui_doc_entry_copy(rolltui_document_add(&history), &doc[0]);
        rolltui_document_remove(&doc, 0);
        if (replying && open_entry > 0) --open_entry;
      }
      written_before = 0;
    }
    inline_mode = !inline_mode;
    open_terminal();
    responder.retarget(term);
    theme_seen = 0;
    if (swap) rolltui_swap_invalidate(swap);
  }

  // A FINISHED ENTRY LEAVES THE REGION for the terminal's own history, drawn as the transcript draws it, where it stays as ordinary
  // output the person scrolls and selects with the terminal's own means. Entries go in order, so one still streaming (a reply, or a
  // command still writing) holds back those after it; `all` commits that one too (leaving: the conversation stays on the screen).
  void commit_finished(bool all) {
    if (!inline_mode || !term) return;
    prepare();  // drawn as the layout on screen now draws it, not the one before a switch
    RolltuiTranscript* tr = transcript();
    if (!tr) return;
    while (doc.size() > 0 && (all || !(replying && open_entry == 0)) && (all || !(running && doc[0].id == running_id))) {
      RolltuiFrame* lines = rolltui_transcript_entry_frame(tr, &doc[0], w, styles, style(ROLLTUI_ROLE_BACKGROUND));
      rolltui_terminal_commit(term, lines);
      rolltui_frame_free(lines);
      rolltui_doc_entry_copy(rolltui_document_add(&history), &doc[0]);
      rolltui_document_remove(&doc, 0);
      if (replying && open_entry > 0) --open_entry;
    }
  }
  // The region's height: what is still live in the transcript, and the prompt's three rows under it. A popup gets room to open.
  int frame_rows() {
    if (!inline_mode) return h;
    prepare();  // the transcript laid out at this width, which is all its height depends on
    const RolltuiTranscript* tr = transcript();
    int rows = 3 + (tr ? static_cast<int>(rolltui_transcript_total_lines(tr)) : 0);
    if (rolltui_window_stack_depth(stack) > 1) rows = std::max(rows, std::min(h, 18));
    return std::clamp(rows, 3, std::max(h, 3));
  }

  // ---- the loop ------------------------------------------------------------------------------

  // THE LOOP `rolltui_run` WOULD WRITE, written here because this app is the one that shows its
  // parts: enter, draw, present at the terminal's depth, sleep until something happens, copy what
  // happened, act on it. The other thread's pieces are taken at the top of each turn.
  // `main` has already asked that both descriptors are terminals.
  int run() {
    inline_mode = rolltui_layout_inline(layout) != 0;
    open_terminal();
    theme_seen = 0;  // the depth is the terminal's to be told, now that there is one
    sync_stores();
    swap = rolltui_swap_new(w, h, style(ROLLTUI_ROLE_BACKGROUND));
    rolltui_terminal_write(term, "\x1b[22;0t", 7);  // keep the person's own title, to put back at the end
    responder.start(term);
    RolltuiStr out;
    Pending pending;
    while (!quit) {
      now = steady_ms();
      sync_stores();  // a layout chosen since the last frame decides where this one is drawn
      if ((rolltui_layout_inline(layout) != 0) != inline_mode) switch_screen();
      take_reply();
      commit_finished(false);
      fh = frame_rows();
      RolltuiFrame* f = rolltui_swap_begin(swap, w, fh, style(ROLLTUI_ROLE_BACKGROUND));
      render_into(f);
      const int timeout = poll_timeout_ms(f);
      rolltui_terminal_present(term, swap, &out);
      write_title();
      pending.ev.clear();
      pending.at.clear();
      pending.text.clear();
      rolltui_terminal_poll(term, timeout, Pending::collect, &pending);
      now = steady_ms();
      for (std::size_t i = 0; i < pending.ev.size() && !quit; ++i) {
        const RolltuiTermEvent& te = pending.ev[i];
        if (te.kind == ROLLTUI_TERM_EVENT_RESIZE) {
          w = te.w;
          h = te.h;
          rolltui_swap_invalidate(swap);
          continue;
        }
        if (te.kind == ROLLTUI_TERM_EVENT_FACTS) {  // a remembered answer was stale: redraw with the right one
          read_facts();
          theme_seen = 0;
          rolltui_swap_invalidate(swap);
          continue;
        }
        if (te.kind > ROLLTUI_TERM_EVENT_FACTS) continue;
        RolltuiEvent e{};
        e.kind = te.kind;
        e.key = te.key;
        e.mouse = te.mouse;
        if (pending.at[i] != static_cast<std::size_t>(-1)) {
          e.text = pending.text.data() + pending.at[i];
          e.text_len = te.text_len;
        }
        handle(e);
      }
      if (editor_wanted) run_editor();
      if (rolltui_transcript_wants_tick(transcript())) rolltui_transcript_tick(transcript());
    }
    responder.stop();  // joined BEFORE the terminal it wakes is freed
    commit_finished(true);  // inline: the conversation, a reply cut short included, stays on the screen
    rolltui_terminal_write(term, "\x1b[23;0t", 7);
    rolltui_str_free(&out);
    rolltui_swap_free(swap);
    swap = nullptr;
    rolltui_terminal_free(term);  // restores the screen
    term = nullptr;
    return 0;
  }
  void read_facts() {
    RolltuiTermFacts f;
    rolltui_terminal_facts(term, &f);
    ambiguous = f.ambiguous_wide ? 1 : 0;
  }
};

int usage() {
  std::fprintf(stderr,
               "usage: rolltui-chat\n"
#ifdef ROLLTUI_SELFTEST
               "       rolltui-chat-selftest --frame WxH [--keys \"Type:hello Enter Tick:5000\"] [--no-strip]\n"
               "                     --keys is selftest/script.hpp's vocabulary; the reply streams on its clock\n"
               "                     --no-strip shows replies as they arrived (the stripping's planted control)\n"
#endif
               "settings come from the menu (F2), /set, or for one run CHAT_THEME, CHAT_LAYOUT, CHAT_THEME_MODE,\n"
               "CHAT_COLOR_DEPTH and CHAT_BINDINGS\n");
  return 2;
}

[[maybe_unused]] bool parse_size(StrView s, int& w, int& h) {
  const std::size_t x = s.find('x');
  if (x == StrView::npos) return false;
  w = std::atoi(rolltui::own(s.first(x)).c_str());
  h = std::atoi(rolltui::own(s.drop_front(x + 1)).c_str());
  return w > 0 && h > 0;
}

}  // namespace

int main(int argc, char** argv) {
  [[maybe_unused]] RolltuiStr frame_spec, keys_spec;
  [[maybe_unused]] bool no_strip = false;
  for (int i = 1; i < argc; ++i) {
    [[maybe_unused]] const StrView a = argv[i];
    [[maybe_unused]] auto next = [&]() -> RolltuiStr { return i + 1 < argc ? RolltuiStr(argv[++i]) : RolltuiStr(); };
    // A PRODUCT TAKES NO CONFIGURATION FLAG (`rolltui-product-flags-test`): what a flag would
    // configure is a setting, and settings have a menu, a command and an environment rung.
#ifdef ROLLTUI_SELFTEST
    if (a == "--frame") frame_spec = next();
    else if (a == "--keys") keys_spec = next();
    else if (a == "--no-strip") no_strip = true;
    else
#endif
      return usage();
  }

#ifdef ROLLTUI_SELFTEST
  const bool headless = !frame_spec.empty();
#else
  const bool headless = false;
#endif
  // ASKED FIRST, before a store is opened or a terminal made, as `rolltui_run` asks: making a terminal enters and
  // writes, and a program refusing to draw must leave neither a pipe nor the person's configuration touched.
  if (!headless && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
    std::fprintf(stderr, "not a terminal (rolltui-chat-selftest --frame WxH renders one)\n");
    return 1;
  }
  App app(/*threaded=*/!headless);
  app.strip_replies = !no_strip;
  app.open_stores(user_presets_dir());
  app.apply_environment();
  app.mount();
  if (!headless) {
    app.history_path = user_presets_dir() + "/chat/history.json";
    app.load_history();
  }
  {
    // END OF INIT: what the screen names that this app does not provide, said before a frame is drawn.
    RolltuiGapReport gaps{};
    rolltui_gaps_collect(app.windows, app.layout, app.bindings, &gaps);
    if (!rolltui_gap_report_clean(&gaps)) {
      RolltuiStr say;
      rolltui_gap_report_summary(&gaps, &say);
      std::fprintf(stderr, "rolltui-chat: %s\n", say.c_str());
    }
    rolltui_gap_report_release(&gaps);
  }

#ifdef ROLLTUI_SELFTEST
  if (headless) {
    if (!parse_size(frame_spec, app.w, app.h)) return usage();
    app.now = 1000;
    app.prepare();
    // A FRAME AFTER EVERY STEP, as the loop does, with the reply released on the step's own clock.
    for (rolltui_selftest::Step& st : rolltui_selftest::scripted_keys(keys_spec, app.w, app.h)) {
      app.now = st.ms;
      app.responder.pump(app.now);
      app.take_reply();
      if (!st.tick) {
        if (st.ev.kind == ROLLTUI_EVENT_PASTE) {
          st.ev.text = st.owned_text.data();
          st.ev.text_len = st.owned_text.size();
        }
        app.handle(st.ev);
        app.responder.pump(app.now);
        app.take_reply();
      }
      app.prepare();
      if (app.quit) break;
    }
    RolltuiSwap* swap = rolltui_swap_new(app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    RolltuiFrame* f = rolltui_swap_begin(swap, app.w, app.h, app.style(ROLLTUI_ROLE_BACKGROUND));
    app.render_into(f);
    RolltuiStr text;
    rolltui_frame_to_text(f, &text);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fprintf(stderr, "title: %s\n", app.title().c_str());
    rolltui_swap_free(swap);
    return 0;
  }
#endif
  return app.run();
}
