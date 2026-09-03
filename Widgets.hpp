#pragma once
//
// rolltui/Widgets.hpp — WIDGETS BY KIND, SOURCES BY NAME (plan/phase-10.md,
// milestone 2). What a window CONTAINS is data: `Windows` reads each window's
// `content` (Layout.hpp's `kind[:source]` table), instantiates the widget for that
// kind, and draws it with whatever the host bound under that source name. Before m2
// each host answered "what does the slot 'transcript' mean?" with an if-chain of its
// own, so a layout file could rearrange windows the host had coded and could not
// introduce a fourth one.
//
// THE RULES, each asserted in rolltui/tests/layout_test.cpp:
//
//   One table, no fallthrough. Every window resolves through Layout.hpp's kind table.
//   An unknown kind, a missing or forbidden source, an unbound source name, a `file:`
//   that cannot be read: each is a NAMED bad value in the report AND an error panel
//   drawn in the window (Role::error). A window is never blank because its content was
//   not understood — that is the failure this milestone exists to remove.
//
//   A widget instance belongs to its CONTENT, not to its window. Two windows with the
//   same content are two views of one widget (one scroll position, one text), and a
//   layout hot-reload keeps every widget that is still named — so changing the
//   arrangement never clears what the user typed or where they had scrolled.
//   Instances are created on demand and never destroyed, so `windows.input("prompt")`
//   is valid whether or not a window currently shows it: a host's editor exists
//   because the host has a prompt, not because a layout drew one this frame.
//
//   The library owns layout / draw / scroll; the HOST owns meaning. A host binds a
//   document, a row source, a submit target — by name, before or after
//   the layout mentions it (bindings are resolved per frame, so binding order is not a
//   rule anyone has to remember). What a menu item DOES, what a submitted line means,
//   what the rows say: still the host's.
//
//   A MENU IS A FILE (Phase 10 m3), not something a host hands over. `menu:<name>`
//   resolves through three rungs, in this order — the same shape as the preset
//   system's precedence, and `menu_origin()` says which one answered:
//     1. <dir>/menus/<name>.json     the user's own, under the preset directory
//                                    (set_dir), re-read when it changes on disk
//     2. add_menu(name, json)        a menu file the HOST carries in its binary
//                                    (roll's presets/menus/*.json, embedded)
//     3. shipped_menu(name)          the library's own (rolltui/presets/menus/)
//   Nothing found is a named bad value with the reason drawn, like every other source.
//   A host still owns what the ids MEAN: it reads the tree back through `menu()`, fills
//   a choice's options, and acts on the MenuEvents. That is what lets a user drop in a
//   menu file, or edit the one that ships, without a rebuild.
//
//   Focus and routing stay the WindowStack's (Layout.hpp). `Windows` never sees an
//   event the stack did not route; `handle()` covers the kinds whose whole event
//   handling is scrolling, and a host takes the typed ones (an Input's InputAction, a
//   Menu's MenuEvent) from the widget itself.
//
//   A widget may SIZE its window: `autosize()` asks each one for the outer extent it
//   wants along its parent's axis (the input grows with its text to half its parent's
//   height, the rule both hosts had) and writes it into the node before layout. That
//   is the only thing a widget writes back into the layout tree.
//
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rolltui/Bindings.hpp"
#include "rolltui/Document.hpp"
#include "rolltui/Input.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Menu.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Transcript.hpp"

namespace rolltui {

// One row of a `rows:` window: a label column and a value that wraps under it.
struct Row {
  std::string label, value;
};

// What every widget needs and no widget owns: the frame's terminal facts and clock.
struct WidgetEnv {
  bool ambiguous_wide = false;
  const Bindings* bindings = nullptr;  // the live table; default_bindings() when null
  std::uint64_t now_ms = 0;
};

class Windows;

// ---- what a window draws with (shared with a host's own registered kinds) -----------

// The rect a widget draws in: the window's inner rect, less one column on each side
// when it is bordered — the widget owns the breathing room inside the frame, since a
// border is the only spacing a layout has (Layout.hpp).
Rect content_rect(const ResolvedNode& rn);

// Wrapped text from line `top`, with the transcript's "▼ N more" marker when there is
// more below. Returns the total wrapped line count (what `top` must be clamped to).
int draw_scrolled_text(const ResolvedNode& rn, Frame& f, const Theme& theme, std::string_view text, int top,
                       bool ambiguous_wide = false);

// The key list a `help` window renders, from the LIVE bindings so it cannot lie about
// a rebinding: an optional lead line, one section per scope, an optional trailing
// note. A host's own `//help` prints the same function of the same table.
std::string help_document(const Bindings& b, std::string_view lead, const std::vector<std::string>& scopes,
                          std::string_view note);

// The input window's sizing rule, as pure functions (the InputWidget applies them):
//   input_max_rows — the cap, HALF THE PARENT'S extent less the border rows, at least 1
//   input_rows     — the rows of text a window shows: the text's rows capped at that,
//                    plus one for a note that cannot sit beside a single row
int input_max_rows(int parent_extent, int border_rows);
int input_rows(int text_rows, int end_col, int note_width, int width, int max_rows);

// The transcript scope's scroll actions applied to a `page`-row view of `total` lines:
// line_up/down, page_up/down, top/bottom. False when the key is not one of them.
bool scroll_by_action(const KeyEvent& k, const Bindings& b, int page, int total, int& top);

// The one interface. A widget is created by kind, keyed by content, and never sees a
// window id — everything it needs comes from the ResolvedNode it is drawn into.
class Widget {
 public:
  virtual ~Widget() = default;
  Content content;  // what it was made from

  // "" when the widget can draw; otherwise the reason, which is both the report's bad
  // value and the error panel's text.
  virtual std::string problem() const { return {}; }
  // Problems that do NOT stop the widget drawing — a menu file's unknown key, which the
  // loaders report rather than ignore. Named in the report exactly like a fatal one; the
  // window still shows the widget.
  virtual std::vector<std::string> notes() const { return {}; }
  // The outer extent this widget wants its window to take along its parent's axis, or
  // nullopt when the layout decides (which is every widget but the input).
  virtual std::optional<int> desired_outer(int /*inner_w*/, int /*parent_extent*/, int /*border*/) const {
    return std::nullopt;
  }
  virtual void layout(const ResolvedNode& rn) = 0;
  virtual void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) = 0;
  // True when the event was consumed. Only the kinds whose whole interaction is
  // scrolling implement this; a host drives an input or a menu itself.
  virtual bool handle(const Event& /*e*/) { return false; }
};

// A Widget over two callbacks, for a host whose own window genuinely has no state of its
// own — roll's approval modal and the studio's editor pane keep theirs beside the thing
// they are a view of. It is a WIDGET the library happens to ship, exactly like
// TextWidget: a host still registers a KIND and still gets a factory, so there is one
// mechanism and not two.
//
// **It is not `bind_custom` under another name**, and the difference is the whole of
// Phase 11 m3: that took a draw function, so a host's window could never receive an
// event and the host had to route by window id. This receives events like any widget, is
// owned by `Windows`, and is keyed by content. A widget with data of its own (a canvas's
// pixels) writes a real class instead — `Widget` is the entire interface either way, and
// nothing here is a shortcut past it.
class CallbackWidget : public Widget {
 public:
  using DrawFn = std::function<void(const ResolvedNode&, Frame&, const Theme&)>;
  using HandleFn = std::function<bool(const Event&)>;
  CallbackWidget(DrawFn draw, HandleFn handle) : draw_(std::move(draw)), handle_(std::move(handle)) {}
  void layout(const ResolvedNode&) override {}
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme) override {
    if (draw_) draw_(rn, f, theme);
  }
  bool handle(const Event& e) override { return handle_ && handle_(e); }

 private:
  DrawFn draw_;
  HandleFn handle_;
};

struct WindowsReport {
  // "window 'panel' (content 'rows:nope'): nothing is bound to 'nope'". Everything a
  // window says is wrong, whether or not it stopped the widget drawing (Widget::notes).
  std::vector<std::string> bad_values;
  bool clean() const { return bad_values.empty(); }
  std::string summary() const;  // the first bad value, with a count when there are more
};

// The per-window widget host. One per frontend; it outlives every layout it is given.
class Windows {
 public:
  Windows();
  ~Windows();
  Windows(const Windows&) = delete;
  Windows& operator=(const Windows&) = delete;

  using RowsFn = std::function<std::vector<Row>()>;
  using SubmitFn = std::function<void(const std::string&)>;
  using TextFn = std::function<std::string()>;
  // A host's own widget kind: a FACTORY, not an instance (Phase 11 m3). What comes back
  // is owned by this Windows, created on demand, keyed by content and never destroyed —
  // so `canvas:left` and `canvas:right` are two canvases for exactly the reason two
  // windows on `transcript:session` are one transcript, and a layout hot-reload keeps
  // the pixels for the reason it keeps the half-typed line.
  using Factory = std::function<std::unique_ptr<Widget>()>;

  // ---- what a host binds (by source name; rebinding replaces) ----
  void bind_document(std::string name, const Document* doc);
  // A document this `Windows` OWNS, built from markdown text — for a tool previewing
  // someone else's app from a profile's sample (AppProfile.hpp), where there is no live
  // document to point at and the sample must outlive the call that supplied it.
  void bind_sample_document(std::string name, std::string markdown);
  void bind_rows(std::string name, RowsFn rows);
  void bind_submit(std::string name, SubmitFn submit);
  // An input's one-line note, drawn beside the prompt when it fits on the first row
  // and on a row of its own otherwise (roll's "working…" hint). Optional.
  void bind_note(std::string name, TextFn note);

  // ---- what a host REGISTERS (by kind name; Phase 11 m3) ----
  // One call registers the NAME with the layout vocabulary (Layout.hpp's
  // `register_widget_kind`, rung 2) and the FACTORY here, so a name can never exist
  // with nothing to build it. `rule` says whether the kind takes a source —
  // `canvas:main` (Required) versus roll's `approval` (Forbidden) — and `source_is`
  // is what a parse error and the design editor call it.
  //
  // Refused, and `why` says which, when the name is one of the LIBRARY's: rung 1 is
  // never shadowed. The library's kinds and a host's are not peers, and this is the one
  // place a host could otherwise quietly replace the input widget for its whole process.
  bool register_kind(std::string name, Factory factory, SourceRule rule = SourceRule::Required,
                     std::string source_is = "the host's own", std::string* why = nullptr);
  // The registered instance for `<kind>:<source>`, or nullptr — the pattern that already
  // exists for transcript(source) / input(source) / menu(source). A host reaches its own
  // widget through this and never through a window id.
  Widget* registered(std::string_view kind, std::string_view source = "");
  // A menu FILE the host carries in its own binary — the middle rung of the three in
  // the header comment. Called once at startup with each of the host's embedded menus;
  // a user's <dir>/menus/<name>.json of the same name wins over it.
  void add_menu(std::string name, std::string json_text);
  // `file:` paths that are not absolute, and `menus/<name>.json`, resolve against this
  // (a host's preset dir).
  void set_dir(std::string dir);
  // What a `help` window renders, from the LIVE bindings: an optional leading line,
  // the scopes to list in order, an optional trailing note.
  void set_help(std::string lead, std::vector<std::string> scopes, std::string note);
  // The scopes an app HAS. Which of them a `help` window lists is the layout's (Phase 11
  // m5b: `help:app`), so a window can be judged against this rather than drawing empty.
  const std::vector<std::string>& help_scopes() const { return help_scopes_; }
  // Every scope the host set, or just one (`help:<scope>`). Also a host's own `//help`.
  std::string help_text(std::string_view scope = {}) const;

  // ---- the frame ----
  void set_env(WidgetEnv env);
  const WidgetEnv& env() const { return env_; }
  const Bindings& bindings() const;

  // Instantiate/reuse a widget per window, autosize the windows that ask, and lay
  // every widget out for `box` — what a host calls once before routing an event and
  // once before composing a frame. The report names every window that cannot draw.
  WindowsReport prepare(WindowStack& stack, Rect box);
  // The pieces, for a host that needs them apart (and for the tests).
  WindowsReport sync(const WindowStack& stack);
  void autosize(WindowStack& stack, Rect box);
  void layout(const WindowStack& stack, Rect box);

  // The slot renderer: `stack.compose(f, box, theme, [&](rn, fr){ windows.draw(rn, fr, theme); })`.
  void draw(const ResolvedNode& rn, Frame& f, const Theme& theme);
  // An event the stack routed to `window`; true when the widget consumed it.
  bool handle(std::string_view window, const Event& e);
  // The input's own handling, for a host that wants the action back: Submit has
  // already delivered the line to the bound target (and pushed it into the history);
  // Ignored is the host's to offer elsewhere; Eof is the host's to act on.
  InputAction input_event(std::string_view source, const Event& e);

  // ---- the widgets, by source (created on demand, never destroyed) ----
  Transcript& transcript(std::string_view source);
  Input& input(std::string_view source);
  // The menu loaded from `menus/<source>.json` — a host fills a choice's options and
  // reads values back through it. An unresolvable name gives an EMPTY menu (and a
  // reported window), never a null: a host's `menu("main").set_value(...)` is valid
  // whether or not the file is there, exactly as `input("prompt")` is.
  Menu& menu(std::string_view source);
  // Which rung answered for `menus/<source>.json`: the file's path, "the host's", "a
  // shipped menu", or "" when nothing did.
  std::string menu_origin(std::string_view source);
  // Every menu name a `menu:` window could resolve right now — the union of the three
  // rungs (the preset directory's menus/*.json, the host's own, the library's shipped),
  // deduplicated and sorted. What the design editor offers as the menu-file choice; a
  // name is offered because a rung has it, never because a host listed it.
  std::vector<std::string> menu_names() const;
  // The extra rows an input window must have whatever its text says (roll holds the
  // input as tall as the modal placed over it).
  void set_input_min_outer(std::string_view source, int rows);

  // ---- what a window holds (routing) ----
  Widget* at(std::string_view window) const;
  std::optional<Content> content_at(std::string_view window) const;
  Transcript* transcript_at(std::string_view window) const;
  Input* input_at(std::string_view window) const;
  Menu* menu_at(std::string_view window) const;

 private:
  friend class WidgetBase;
  Widget* widget_for(const std::string& content);

  WidgetEnv env_;
  std::string dir_;
  std::string help_lead_, help_note_;
  std::vector<std::string> help_scopes_;
  std::map<std::string, const Document*> documents_;
  std::map<std::string, Document> owned_documents_;  // bind_sample_document's
  std::map<std::string, RowsFn> rows_;
  std::map<std::string, SubmitFn> submits_;
  std::map<std::string, TextFn> notes_;
  std::map<std::string, std::string> host_menus_;  // add_menu: name → the file's text
  std::map<std::string, Factory> factories_;       // register_kind: kind name → how to build one
  std::map<std::string, std::unique_ptr<Widget>> by_content_;
  std::map<std::string, Widget*, std::less<>> by_window_;
};

}  // namespace rolltui
