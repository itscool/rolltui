//
// bindings_test.cpp — key bindings as data (milestone 17): chords parse and print in
// every modifier order and round-trip; the shipped default embeds its file verbatim,
// loads clean, and binds every library action; lookup is by scope so one chord serves
// several widgets; a conflict inside a scope is reported and the first binding wins;
// the Enter rule refuses a file that moves Enter and restores it; bind() moves a chord
// off a conflicting action and says so; to_json round-trips; help_lines renders the
// live table. Phase 10 m4: the app scope leaves the library's table — a layout
// declares it, a file's chords for an undeclared action are kept and inert, and
// declaring makes them live. Phase 11 m1: the library's own TOOLS' scopes leave it
// too — one declare() takes the layout's actions and the mounted tool's, a tool's
// suggested chord fills a gap and never overrides, and a Phase 10 file naming
// playground.quit still loads clean and keeps its row.
//
#include <fstream>
#include <string>

#include "rolltui/Bindings.hpp"
#include "rolltui/Layout.hpp"  // m4: shipped_default_actions()
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

#ifndef ROLLTUI_BINDINGS_DIR
#error "ROLLTUI_BINDINGS_DIR must point at rolltui/presets/bindings"
#endif

namespace {
KeyEvent key(Key k, bool ctrl = false, bool alt = false, bool shift = false) { KeyEvent e; e.key = k; e.ctrl = ctrl; e.alt = alt; e.shift = shift; return e; }
KeyEvent ch(char32_t c, bool ctrl = false, bool alt = false) { KeyEvent e; e.key = Key::Char; e.ch = c; e.ctrl = ctrl; e.alt = alt; return e; }
std::string read_file(const std::string& p) { std::ifstream in(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }
}  // namespace

int main() {
  // ---- chords ----
  check(parse_chord("ctrl+w") == ch('w', true) && parse_chord("Ctrl+W") == ch('w', true) && parse_chord("C+w") == ch('w', true), "ctrl+w in any case or abbreviation");
  check(parse_chord("shift+ctrl+left") == key(Key::Left, true, false, true) && parse_chord("ctrl+shift+left") == key(Key::Left, true, false, true), "modifiers in any order");
  check(parse_chord("alt+enter") == key(Key::Enter, false, true) && parse_chord("meta+enter") == key(Key::Enter, false, true) && parse_chord("option+enter") == key(Key::Enter, false, true), "alt, meta and option are one modifier");
  check(parse_chord("f1") == key(Key::F1) && parse_chord("F12") == key(Key::F12) && parse_chord("escape") == key(Key::Escape) && parse_chord("esc") == key(Key::Escape) && parse_chord("pgdn") == key(Key::PageDown), "named keys and their short forms");
  check(parse_chord("?") == ch(U'?') && parse_chord("space") == ch(U' ') && parse_chord("+") == ch(U'+') && parse_chord("ctrl++") == ch(U'+', true), "a printable character is itself; '+' and 'ctrl++' parse");
  check(!parse_chord("") && !parse_chord("ctrl+") && !parse_chord("hyper+x") && !parse_chord("ctrl+meta+x+y") && !parse_chord("f13"), "empty, dangling, unknown modifier, two keys, f13: not chords");
  check(chord_to_string(key(Key::Left, true, false, true)) == "ctrl+shift+left" && chord_to_string(ch(U'?')) == "?" && chord_to_string(ch(U' ')) == "space" && chord_to_string(key(Key::PageUp)) == "pageup",
        "chord_to_string is canonical (ctrl, alt, shift; key names)");
  {
    bool all = true;
    for (const char* c : {"ctrl+shift+left", "alt+enter", "f1", "escape", "?", "space", "shift+tab", "ctrl+home", "alt+backspace", "pagedown"})
      all &= chord_to_string(*parse_chord(c)) == c;
    check(all, "canonical chords round-trip through parse and print");
    KeyEvent raw = key(Key::Left, true);
    raw.raw = "\x1b[1;5D";
    check(chord_to_string(raw) == "ctrl+left", "a KeyEvent's raw bytes are never part of a chord");
    check(chord_display(*parse_chord("ctrl+shift+left")) == "Ctrl-Shift-Left" && chord_display(*parse_chord("alt+enter")) == "Alt-Enter" && chord_display(*parse_chord("?")) == "?" &&
              chord_display(*parse_chord("pageup")) == "PgUp" && chord_display(*parse_chord("f1")) == "F1",
          "chord_display is the help form (Ctrl-Shift-Left, Alt-Enter, PgUp)");
    check(chord_to_string(key(Key::Unknown)).empty(), "an Unknown key has no chord");
  }
  // ---- the shipped default ----
  {
    check(default_bindings_json() == read_file(std::string(ROLLTUI_BINDINGS_DIR) + "/default.json") && !default_bindings_json().empty(), "the default embeds presets/bindings/default.json verbatim");
    BindingsLoadReport rep;
    std::optional<Bindings> d = Bindings::from_json(default_bindings_json(), rep);
    check(d && rep.clean(), "the shipped default loads clean [" + rep.summary() + "]");
    bool every_bound = true;
    std::string unbound;
    for (const std::string& a : d->actions())
      if (d->chords_for(a).empty()) { every_bound = false; unbound += " " + a; }
    check(every_bound, "every library action has at least one chord in the default" + unbound);
    check(d->actions().size() == library_actions().size(), "the table lists exactly the library's actions (" + std::to_string(d->actions().size()) + ")");
    const Bindings& b = default_bindings();
    check(b.action_for(key(Key::Enter), "input") == "input.submit" && b.action_for(key(Key::Enter), "menu") == "menu.activate" && b.action_for(key(Key::Enter), "transcript").empty(),
          "Enter is input.submit in the input scope, menu.activate in the menu scope, nothing in the transcript");
    check(b.action_for(key(Key::Up), "input") == "input.up" && b.action_for(key(Key::Up), "transcript") == "transcript.line_up" && b.action_for(key(Key::Up), "menu") == "menu.up",
          "Up serves three scopes");
    check(b.action_for(ch('w', true), "input") == "input.kill_word_backward" && b.action_for(key(Key::Backspace, false, true), "input") == "input.kill_word_backward", "two chords, one action");
    check(b.action_for(key(Key::Left, true, false, true), "input") == "input.select_word_left" && b.action_for(key(Key::Left, false, true, true), "input") == "input.select_word_left", "ctrl+shift+left and alt+shift+left both extend by a word");
    check(b.action_for(ch(U'?'), "app") == "app.help" && b.action_for(key(Key::F1), "app") == "app.help" && b.action_for(ch(U'?'), "input").empty(), "'?' is app.help and is not an input action (typing it inserts)");
    KeyEvent with_raw = ch('w', true);
    with_raw.raw = "\x17";
    check(b.action_for(with_raw, "input") == "input.kill_word_backward", "lookup ignores raw bytes");
    check(b.chords_text("input.kill_word_backward") == "Ctrl-W, Alt-Backspace", "chords_text joins the display forms [" + b.chords_text("input.kill_word_backward") + "]");
    check(scope_of("input.submit") == "input" && scope_of("app.help") == "app", "scope_of");
  }
  // ---- Phase 10 m4: the app scope is a LAYOUT's, not the library's ----
  {
    bool any_app = false;
    for (const ActionInfo& a : library_actions()) any_app |= scope_of(a.name) == "app";
    check(!any_app, "library_actions() declares no app.* action — the layout does (m4)");
    check(library_scope("input") && library_scope("transcript") && library_scope("menu") && library_scope("edit") && library_scope("stack") &&
              !library_scope("app") && !library_scope("editor") && !library_scope("playground") && !library_scope("mine"),
          "library_scope: the WIDGET scopes are the library's; app, the tools' and a host's own are not (Phase 11 m1)");

    // A file's chords for an undeclared action are KEPT and inert, never dropped: a
    // bindings file is global and a user's, while the actions are the screen's.
    BindingsLoadReport rep;
    std::optional<Bindings> b = Bindings::from_json(R"({"name":"x","bindings":{"app.help":["f1","?"],"other.thing":["f9"],"input.sumbit":["f8"]}})", rep);
    check(b && rep.unknown_actions == std::vector<std::string>{"input.sumbit"},
          "a typo in a LIBRARY scope is an unknown action; a name in any other scope is not");
    check(b && !b->has("app.help") && b->action_for(key(Key::F1), "app").empty() && b->chords_for("app.help").size() == 2,
          "an undeclared action keeps its chords and never answers a key");
    check(b && b->undeclared() == std::vector<std::string>{"app.help", "other.thing"}, "undeclared() names them, in table order");
    BindingsLoadReport rep2;
    std::optional<Bindings> round = Bindings::from_json(b->to_json("x"), rep2);
    check(round && round->chords_for("app.help").size() == 2 && *round == *b,
          "…and they survive the round trip, so a file written on one screen keeps its keys on another");

    b->declare({{"app.help", "open help"}});
    check(b->has("app.help") && b->action_for(key(Key::F1), "app") == "app.help" && b->chords_for("app.help").size() == 2 &&
              b->description("app.help") == "open help",
          "declaring the action makes the kept chords live, with its description");
    check(b->undeclared() == std::vector<std::string>{"other.thing"}, "…and only the still-undeclared ones remain");
    b->declare({{"app.help", "SOMETHING ELSE"}});
    check(b->description("app.help") == "SOMETHING ELSE",
          "re-declaring updates the description — a hot-reloaded layout file may change what an action does");
    b->declare({{"app.help", "open help"}});
    check(help_lines(*b, "app").size() == 1 && help_lines(*b, "app")[0].find("F1, ?") == 0,
          "help renders an action known only because a layout declared it [" + (help_lines(*b, "app").empty() ? "" : help_lines(*b, "app")[0]) + "]");

    // m6: declare() is AUTHORITATIVE, not additive — the actions of the screen you are on,
    // not of every screen you have been on. Found by the files-only proof: a runtime layout
    // switch left the previous layout's five app actions live under a layout declaring one.
    b->declare({{"app.zoom", "zoom in"}});
    check(!b->has("app.help") && b->action_for(key(Key::F1), "app").empty() && b->chords_for("app.help").size() == 2 &&
              b->has("app.zoom") && help_lines(*b, "app").size() == 1,
          "a layout that stops declaring an action makes it inert again — its chords kept, nothing emitting it");
    check(b->has("input.submit") && b->has("menu.activate") && b->has("stack.close_popup"),
          "…and the library's own closed scopes are untouched by any declaration");
    b->declare({{"app.help", "open help"}});  // put the block's screen back for what follows

    // Two UNDECLARED actions of one scope still conflict at load — the check runs over
    // the rows, not through action_for, which skips them.
    BindingsLoadReport rep3;
    std::optional<Bindings> c2 = Bindings::from_json(R"({"name":"c","bindings":{"app.one":["f9"],"app.two":["f9"]}})", rep3);
    check(c2 && rep3.conflicts.size() == 1 && rep3.conflicts[0].find("'f9' bound to both app.one and app.two") == 0,
          "a chord bound twice in one undeclared scope is still a conflict");

    // default_bindings() is the shipped bindings over the shipped default LAYOUT.
    const Bindings& d2 = default_bindings();
    check(d2.has("app.help") && d2.description("app.menu") == "open the settings and commands menu",
          "default_bindings() carries the shipped default layout's declared actions");
    check(d2.actions().size() == library_actions().size() + shipped_default_actions().size(),
          "…exactly those and the library's, nothing else");
    check(d2.undeclared().empty(), "…and the shipped bindings bind nothing the shipped layouts do not declare");
  }
  // ---- Phase 11 m1: a TOOL's scope is not the library's ----------------------------
  // library_actions() closes over the WIDGET scopes only. `editor.*` and `playground.*`
  // are the library's own tools' — one application's, not every host's — so whoever
  // MOUNTS a tool declares them, and a host that mounts none advertises none.
  {
    bool tool_scope = false;
    std::string named;
    for (const ActionInfo& a : library_actions())
      if (const std::string_view s = scope_of(a.name); s == "editor" || s == "playground" || s == "app") { tool_scope = true; named = a.name; }
    check(!tool_scope, "library_actions() declares no tool action: a scope is closed because the library DEFINES it, not because it SHIPS the tool [" + named + "]");
    // The same rule for the file that ships beside it: it belongs to every host, so a
    // `playground.quit` row in it would be a key every host advertises and cannot press.
    // (default_bindings() aborts on this; asserted here so the failure has a name.)
    BindingsLoadReport srep;
    std::optional<Bindings> shipped = Bindings::from_json(default_bindings_json(), srep);
    std::string stray;
    for (const std::string& a : shipped->undeclared())
      if (scope_of(a) != "app") stray += " " + a;
    check(shipped && stray.empty(), "the shipped bindings file binds the library's widgets and the shipped screen's app.* and nothing else —" + (stray.empty() ? " none" : stray));

    // A PHASE 10 BINDINGS FILE still loads clean and keeps its rows. This is the mercy
    // the whole split depends on, and which side of the table a scope sits on is what
    // decides it: a typo in a LIBRARY scope is an unknown action, while `playground.quit`
    // — now in nobody's closed set — is kept, inert, until something declares it.
    const char* phase10 = R"({"name":"p10","bindings":{"input.submit":["enter"],"app.help":["f1"],
        "editor.undo":["ctrl+z"],"playground.quit":["ctrl+q"],"playground.reload":["f5"]}})";
    BindingsLoadReport prep;
    std::optional<Bindings> p = Bindings::from_json(phase10, prep);
    check(p && prep.clean(), "a Phase 10 bindings file binding playground.quit loads CLEAN [" + prep.summary() + "]");
    check(p->chords_for("playground.quit").size() == 1 && !p->has("playground.quit") && p->action_for(ch('q', true), "playground").empty(),
          "…its row is kept and inert: nothing has mounted the playground, so nothing emits it");
    BindingsLoadReport rt;
    std::optional<Bindings> back = Bindings::from_json(p->to_json("p10"), rt);
    check(back && rt.clean() && *back == *p, "…and it survives the round trip, so `bindings save` never loses another program's keys");

    // MOUNTING the tool: one authoritative declare() takes the layout's actions and the
    // tool's, and the tool's suggested chord fills only a GAP.
    const std::vector<ToolAction> tool = {{"playground.quit", "quit", "ctrl+q"},
                                          {"playground.reload", "reload the fixture", "f5"},
                                          {"playground.cycle_theme", "cycle the shipped theme presets", "f3"}};
    p->declare({{"app.help", "open help"}}, tool);
    check(p->has("playground.quit") && p->action_for(ch('q', true), "playground") == "playground.quit" && p->has("app.help"),
          "declaring the layout's actions and the mounted tool's in ONE call makes both live");
    check(p->chords_for("playground.cycle_theme").size() == 1 && p->action_for(key(Key::F3), "playground") == "playground.cycle_theme",
          "…an action the file never named gets the tool's suggested chord (the gap it is for)");
    check(p->chords_for("playground.quit").size() == 1 && p->chords_for("playground.reload").size() == 1,
          "…and one it did named keeps exactly the file's row: a suggestion never overrides");
    // The order trap this rule was first got wrong on: a declaration creates an empty row
    // for its action, so a suggestion made AFTER one would decline every time and every
    // tool key would be silently unbound. One call, one order.
    Bindings fresh;
    fresh.declare({}, tool);
    check(fresh.action_for(ch('q', true), "playground") == "playground.quit" && fresh.action_for(key(Key::F5), "playground") == "playground.reload",
          "a mounted tool's keys work on a table that had never heard of it");

    // The three ways a suggestion is DECLINED, all leaving the action declared-and-unbound
    // rather than absent or sharing a chord.
    Bindings unbound;
    BindingsLoadReport urep;
    unbound = *Bindings::from_json(R"({"name":"u","bindings":{"playground.quit":[],"playground.reload":["ctrl+q"]}})", urep);
    unbound.declare({}, tool);
    check(unbound.has("playground.quit") && unbound.chords_for("playground.quit").empty(),
          "an EMPTY row wins too — a file (or a user) said 'unbound', and a suggestion must not bring the key back");
    check(unbound.action_for(ch('q', true), "playground") == "playground.reload", "…and the chord the file moved stays where the file put it");
    Bindings clash;
    clash.declare({}, {{"mine.one", "one", "f9"}, {"mine.two", "two", "f9"}, {"mine.three", "three", "not+a+chord"}});
    check(clash.action_for(key(Key::F9), "mine") == "mine.one" && clash.has("mine.two") && clash.chords_for("mine.two").empty(),
          "a suggestion whose chord already serves the scope is declined: the action is declared UNBOUND, not a second holder of one chord");
    check(clash.has("mine.three") && clash.chords_for("mine.three").empty(), "…and an unparseable chord is the same: visible as (unbound), never missing");
    // Authoritative still: the tools survive a screen change because they are passed
    // every time; the last screen's app actions do not.
    p->declare({{"app.zoom", "zoom in"}}, tool);
    check(!p->has("app.help") && p->has("app.zoom") && p->action_for(ch('q', true), "playground") == "playground.quit",
          "a new screen replaces the layout's actions and keeps the mounted tool's");
    p->declare({{"app.zoom", "zoom in"}}, {});
    check(!p->has("playground.quit") && p->chords_for("playground.quit").size() == 1,
          "…and UNmounting the tool makes its actions inert again, chords kept: nothing else can advertise them");
  }
  // ---- the loader's report ----
  {
    BindingsLoadReport rep;
    std::optional<Bindings> b = Bindings::from_json(R"({"name":"x","bindings":{"input.left":["left","ctrl+b"],"input.right":["left"],"input.nothing":["x"],"input.up":["meta+hyper+z"],"input.newline":["enter"],"input.submit":["ctrl+j"]},"extra":1})", rep);
    check(b && rep.error.empty(), "a file with problems still loads");
    check(rep.conflicts.size() == 1 && rep.conflicts[0].find("'left' bound to both input.left and input.right") == 0 && b->action_for(key(Key::Left), "input") == "input.left",
          "a chord bound twice in one scope is a conflict; the first binding wins [" + (rep.conflicts.empty() ? "" : rep.conflicts[0]) + "]");
    check(rep.unknown_actions == std::vector<std::string>{"input.nothing"}, "an unknown action is reported by name");
    check(rep.bad_chords.size() == 1 && rep.bad_chords[0].find("input.up: 'meta+hyper+z'") == 0, "an unparseable chord is reported with its action");
    check(rep.bad_values.size() == 2 && rep.bad_values[0].find("input.newline: 'enter' is always input.submit") == 0 && rep.bad_values[1].find("input.submit: 'enter' is always bound") == 0,
          "the Enter rule: binding Enter elsewhere in the input scope is refused by name, and input.submit gets Enter back");
    check(b->action_for(key(Key::Enter), "input") == "input.submit" && b->action_for(ch('j', true), "input") == "input.submit", "…so Enter submits, and ctrl+j too");
    check(rep.unknown_keys == std::vector<std::string>{"extra"}, "an unknown top-level key is reported");
    check(!Bindings::from_json("[1]", rep) && !rep.error.empty(), "a non-object is unusable");
    check(!Bindings::from_json(R"({"name":"x"})", rep) && rep.error.find("bindings") != std::string::npos, "a file without a bindings object is unusable");
    std::optional<Bindings> empty = Bindings::from_json(R"({"name":"e","bindings":{}})", rep);
    check(empty && empty->chords_for("input.left").empty() && !empty->chords_for("input.submit").empty(), "an empty file binds nothing but Enter → submit (a file is the whole domain)");
  }
  // ---- bind / unbind ----
  {
    Bindings b = default_bindings();
    std::string moved;
    check(b.bind("input.word_left", *parse_chord("alt+b"), &moved) && moved.empty() && b.action_for(ch('b', false, true), "input") == "input.word_left", "bind adds a chord");
    check(b.bind("input.word_right", *parse_chord("alt+d"), &moved) && moved == "input.kill_word_forward" && b.action_for(ch('d', false, true), "input") == "input.word_right" &&
              b.action_for(key(Key::Delete, true), "input") == "input.kill_word_forward",
          "a chord bound elsewhere in the scope moves, and moved_from names the loser");
    check(!b.bind("input.newline", key(Key::Enter)) && b.action_for(key(Key::Enter), "input") == "input.submit", "Enter cannot be bound to another input action");
    check(!b.bind("input.nope", key(Key::F9)), "an unknown action is refused");
    check(b.bind("transcript.top", key(Key::Enter)) && b.action_for(key(Key::Enter), "transcript") == "transcript.top", "…but Enter may serve another scope");
    check(!b.unbind("input.submit", key(Key::Enter)) && b.unbind("input.word_left", *parse_chord("alt+b")) && !b.unbind("input.word_left", *parse_chord("alt+b")), "unbind: Enter stays on submit; a chord removes once");
    b.clear("input.copy");
    check(b.chords_for("input.copy").empty(), "clear empties an action");
    b.clear("input.submit");
    check(!b.chords_for("input.submit").empty(), "…except input.submit");
    b.add_action("mine.thing", "my host's own");
    check(b.bind("mine.thing", key(Key::F9)) && b.action_for(key(Key::F9), "mine") == "mine.thing" && b.description("mine.thing") == "my host's own", "a host may add its own actions");
  }
  // ---- round trip ----
  {
    const Bindings& d = default_bindings();
    BindingsLoadReport rep;
    std::optional<Bindings> back = Bindings::from_json(d.to_json("default"), rep);
    check(back && rep.clean() && *back == d, "to_json / from_json round-trips the default exactly");
    Bindings edited = d;
    edited.bind("input.word_left", *parse_chord("alt+b"));
    check(!(edited == d), "an edit makes the tables unequal (the label-by-comparison rule can stand on this)");
  }
  // ---- help ----
  {
    const std::vector<std::string> lines = help_lines(default_bindings(), "input", {"input.submit", "input.kill_word_backward"});
    check(lines.size() == 2 && lines[0].find("Enter") == 0 && lines[0].find("send the line") != std::string::npos && lines[1].find("Ctrl-W, Alt-Backspace") == 0,
          "help_lines: the chords, then the description [" + (lines.empty() ? "" : lines[0]) + "]");
    const std::vector<std::string> all = help_lines(default_bindings(), "menu");
    check(all.size() == 11 && all[0].find("Up") == 0, "an empty list means every action of the scope (menu: 11)");
    Bindings vim = default_bindings();
    vim.bind("input.word_left", *parse_chord("alt+b"));
    check(help_lines(vim, "input", {"input.word_left"})[0].find("Alt-B") != std::string::npos, "help follows a rebinding: it is rendered from the live table");
    // The chord column is capped at 24 cells: one long chord list does not push every
    // other description across a narrow popup (found by a 46-column golden).
    Bindings wide = default_bindings();
    wide.bind("input.word_right", *parse_chord("ctrl+shift+f12"));
    wide.bind("input.word_right", *parse_chord("alt+shift+f11"));
    const std::vector<std::string> capped = help_lines(wide, "input", {"input.left", "input.word_right"});
    check(capped[0].find("move one grapheme left") <= 24, "a short chord's description starts within the 24-cell column (at " + std::to_string(capped[0].find("move one grapheme left")) + ")");
    check(capped[1].find("  move one word right") != std::string::npos && capped[1].find("move one word right") > 24,
          "a chord list longer than the column is followed by two spaces, not padded");
  }
  return report("rolltui bindings_test");
}
