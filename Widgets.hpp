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
// OWNERSHIP, stated here because this is the type that does most of it (Phase 13 m2, and
// asserted in rolltui/tests/ownership_test.cpp). The library has THREE shapes and no
// fourth:
//
//   OWNED     one owner, and it is a `unique_ptr` or a value member. `Windows` owns every
//             widget (`map<string, unique_ptr<Widget>>`, keyed by content and never
//             destroyed); `WindowStack` owns its layers BY VALUE; a `Frame` owns its cells.
//   BORROWED  every raw `T*` and every `string_view` crossing this API. A borrow never
//             owns and never frees: `bind_document(name, const Document*)` takes the
//             HOST's document and the host is what keeps it alive, `WidgetEnv::bindings`
//             points at the table for THIS frame, and `at()` / `transcript()` hand back a
//             widget this `Windows` still owns.
//   VALUE     everything else — Content, Style, Layer, Row, Mark. Copied, not referenced.
//
// **THERE IS NO SHARED OWNERSHIP, and a test fails if one appears.** A `shared_ptr` makes
// a lifetime a runtime question, and every lifetime here is structural. Every `shared_ptr`
// in the repository belongs to a HOST (the preset stores, roll's SessionView) — which is
// the right place for one, and outside this library.
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
#include "rolltui/c/rolltui_widget_kinds.h"
#include "rolltui/c/rolltui_widgets.h"
#include "rolltui/Document.hpp"
#include "rolltui/Input.hpp"
#include "rolltui/Layout.hpp"
#include "rolltui/Menu.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Transcript.hpp"

namespace rolltui {

// One row of a `rows:` window: a label column and a value that wraps under it. `Rows` is
// what a host fills instead of returning a fresh vector every frame (Phase 13 m5b, and
// CLAUDE.md's per-frame-API rule): `reset()` keeps the array's capacity AND every row's
// string buffers, so `add()` on a warm frame assigns into storage that already exists and
// allocates nothing. Phase 15 m5e/17: BOTH ARE THE C STRUCTS now (`rolltui/c/rolltui_widgets.h`
// — one definition, since the `rows` kind is a plugin the C boundary owns), so `add()` on a
// live `Rows&` still says what it said; only what BUILDS the rows moved.
using Row = RolltuiRow;
using Rows = RolltuiRows;

// An input's one-line note, and — since Phase 12 m6 — what STATE it is in. A host that
// has no motion to report returns a bare string and the implicit conversion does the
// rest; roll's "working…" returns `{text, EffectState::Waiting, when the turn started}`,
// which is the whole of roll's waiting-for-first-token indicator. Nothing here says what
// waiting looks like: the theme does (rolltui/Effects.hpp), and a theme that maps nothing
// leaves the still text the host drew. Phase 15 m5e/17: `Note` IS `RolltuiNote` — the `input`
// kind is a plugin now, and `bind_note`'s callback crosses a `RolltuiNote*` the way every
// other host binding does; a host's `[](Note& out) { out = "working"; }` still compiles
// unchanged because the converting constructors below are the C struct's own.
using Note = RolltuiNote;

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
//
// PHASE 15 m5 — THESE VIRTUALS ARE ONE VTABLE STRUCT AT THE BOUNDARY
// (`rolltui/c/rolltui_widgets.h`), AND THE RULE FOR ADDING TO THEM IS STATED THERE. Nothing
// a host writes changed: `Windows` wraps every widget it builds — its own seven kinds and a
// host's alike — in a `RolltuiWidget` whose `self` is the object below. What changed is that
// the SET is now enumerable, so a tenth question has to say, in writing and before its first
// caller exists, what a widget that has never heard of it does. Read that rule before adding
// a virtual here; a virtual added here without a slot there is invisible to the window.
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

  // ---- SCROLL, as a capability a widget OPTS INTO (Phase 12 m5) --------------------
  // The window draws the bar and reads the mouse on it; the WIDGET remains the sole
  // owner of its position. That split is forced, not stylistic: the transcript keeps
  // its position as an ANCHOR (entry, line-within-entry) so a re-wrap shows the same
  // entry at the top, and a `{first, visible, total}` cached in the window would go
  // stale exactly when a re-wrap changes the total AND the offset's meaning at once,
  // with nothing to check it. So the window ASKS and COMMANDS; it never stores.
  //
  // TWO OPTIONAL HALVES, because they are genuinely different capabilities:
  //   scroll_extent()  REPORTS  → the window may draw a bar
  //   scroll_to(first) ACCEPTS  → the bar may be dragged and its trough clicked
  // A menu's scroll is DERIVED from its selection, so dragging its thumb would move the
  // view away from the selected row: it reports and declines to be commanded, and gets
  // an accurate bar that is not a handle. Collapsing the halves would force every
  // widget into a behaviour only some of them want.
  //
  // Declared PER AXIS from the start even though only Vertical is implemented — a wide
  // table or code block will want the horizontal one, and a vertical-only assumption is
  // one enum parameter to avoid now and a rewrite to retrofit.
  enum class Axis : std::uint8_t { Vertical = ROLLTUI_AXIS_VERTICAL, Horizontal = ROLLTUI_AXIS_HORIZONTAL };
  using ScrollExtent = RolltuiScrollExtent;
  virtual std::optional<ScrollExtent> scroll_extent(Axis /*axis*/) const { return std::nullopt; }
  // False when this widget reports but will not be driven. Anything that returns true
  // must clamp: the window passes what the pointer implies, not what is valid.
  virtual bool scroll_to(Axis /*axis*/, std::size_t /*first*/) { return false; }
};

// ---- the bar's geometry, as a pure function -----------------------------------------
// Where the thumb sits and how long it is, in the track's own cells. Separate from every
// widget and every window so it can be a table test — including the degenerate sizes
// this project insists on (a 0- or 1-cell track, an empty document, a viewport larger
// than the content). Returns false when no bar should be drawn at all.
struct ScrollThumb {
  int offset = 0;  // cells from the track's start
  int length = 0;  // cells, always >= 1 when drawn
};
bool scroll_thumb(const Widget::ScrollExtent& e, int track, ScrollThumb& out);
// The inverse, for a click or a drag: the `first` line that puts the thumb's START at
// `cell` of the track. Clamped to a valid first line.
std::size_t scroll_first_for_cell(const Widget::ScrollExtent& e, int track, int cell);

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
  // The handle, for the shim's own factories and for nothing else.
  RolltuiWindows* handle() { return w_.get(); }
  Windows(const Windows&) = delete;
  Windows& operator=(const Windows&) = delete;

  // FILL-A-CALLER'S-BUFFER, not return-by-value (Phase 13 m5b; the rule is in CLAUDE.md).
  // Both of these are called once or more per frame, and both used to hand back a freshly
  // built container — one allocation each, every frame, for data that usually did not
  // change. `TextFn` was a third and is gone: nothing used it.
  using RowsFn = std::function<void(Rows&)>;
  using SubmitFn = std::function<void(const std::string&)>;
  using NoteFn = std::function<void(Note&)>;
  // A host's own widget kind: a FACTORY, not an instance (Phase 11 m3). What comes back
  // is owned by this Windows, created on demand, keyed by content and never destroyed —
  // so `canvas:left` and `canvas:right` are two canvases for exactly the reason two
  // windows on `transcript:session` are one transcript, and a layout hot-reload keeps
  // the pixels for the reason it keeps the half-typed line.
  using Factory = std::function<std::unique_ptr<Widget>()>;
  // OWNED, through a `unique_ptr` with a deleter that calls the C free.
  struct Handle {
    void operator()(RolltuiWindows* p) const { rolltui_windows_free(p); }
  };

  // ---- what a host binds (by source name; rebinding replaces) ----
  void bind_document(std::string name, const Document* doc);
  // A document this `Windows` OWNS, built from markdown text — for a tool previewing
  // someone else's app from a profile's sample (AppProfile.hpp), where there is no live
  // document to point at and the sample must outlive the call that supplied it.
  void bind_sample_document(std::string name, std::string markdown);
  void bind_rows(std::string name, RowsFn rows);
  // What Enter MEANS for this input, which is the HOST's to say and not the widget's.
  // A PROMPT sends a line and starts a new one — the text was a message, and it is now
  // gone and in the history. A FIND BAR (or a filter, or a rename field) keeps its text,
  // because the text is a standing QUERY: clearing it would erase the very thing the
  // Enter was about, and put nothing in a history that has no use for it. Until Phase 12
  // m4 every input got the prompt's behaviour, which made the find bar clear itself on
  // its own "next match" key — correct-looking code, silently wrong.
  enum class OnSubmit { SendAndClear, Keep };
  void bind_submit(std::string name, SubmitFn submit, OnSubmit on_submit = OnSubmit::SendAndClear);
  // Phase 15 m6: the map this read moved to the boundary with `submits_` itself, since a host
  // sets both together (`bind_submit`) and a widget reads them at different times (a submit,
  // then separately whether to clear). `rolltui_windows_on_submit` returns 0/1 for the same
  // two values this enum names, and 0 (SendAndClear) is what "nothing bound" answers too.
  OnSubmit on_submit_for(std::string_view name) const {
    return rolltui_windows_on_submit(w_.get(), name.data(), name.size()) ? OnSubmit::Keep : OnSubmit::SendAndClear;
  }
  // An input's one-line note, drawn beside the prompt when it fits on the first row and
  // on a row of its own otherwise (roll's "working…" hint). Optional. Returning a bare
  // string is still valid and still means "no motion" — see Note above.
  void bind_note(std::string name, NoteFn note);

  // ---- what a transcript does with a long code block, and how it colours one -------
  // Both are HOST facts, and both are off until a host says otherwise (Phase 12 m5b).
  //
  // The highlighter is the m2 seam: the library ships no highlighter, and an unset one
  // is never invoked, so registering `rolltui::diff_spans` is a HOST declaring that its
  // documents' ```diff fences mean a diff. Nothing here ever sniffs a block's content.
  //
  // The thresholds are the fold and the cap (Markdown.hpp). A host picks them because
  // they are a judgement about ITS transcript's shape, not a library constant.
  void set_highlighter(markdown::Highlighter h);
  void set_code_fold(int fold_over_lines, int cap_lines);
  const markdown::Highlighter& highlighter() const { return highlighter_; }
  // Phase 17 m1c: pushed straight onto every LIVE `Transcript` (`transcripts_`, below) the
  // moment it is set, and onto each new one as it is created — so a transcript picks up a
  // LATER highlighter instead of silently keeping the first, with no epoch for a widget to
  // poll. Before the port this was PULLED once per frame, by a widget comparing its own
  // last-seen epoch against this counter; there is no such per-frame pull left to drive
  // (the transcript kind is pure C now and never touches the highlighter), so nothing reads
  // an epoch anymore and none is kept.
  int code_fold_over_lines() const { return code_fold_over_lines_; }
  int code_cap_lines() const { return code_cap_lines_; }

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
  Widget* widget_for(const std::string& content);
  // The library's own seven kinds and the error panel, registered through the same call a
  // host uses (rolltui/c/rolltui_widgets.h's rule 5).
  void register_builtin_kinds();
  WidgetEnv env_;
  std::string help_lead_, help_note_;
  std::vector<std::string> help_scopes_;
  // `bind_sample_document`'s OWNED copy, for a tool previewing another app's sample content
  // with no live document to point at (AppProfile.hpp). Stays a C++ map: unlike the BORROW
  // half (`documents_`, moved to the boundary below), this OWNS a `Document` value on a path
  // called once per named source rather than per frame, and moving it would need either a
  // fragile reinterpret through `RolltuiDocument` or a second owner — neither pays for
  // itself the way the callback maps below do.
  std::map<std::string, Document> owned_documents_;
  // Phase 17 (widget kinds port): `Windows` OWNS every `input:<source>` here now, one per
  // source, created on demand exactly like `owned_documents_` above. The reason is
  // `input()`/`input_at()`'s own contract: both must hand back a LIVE `Input&` backed by the
  // SAME state the `input` kind (a pure-C plugin now, `rolltui/c/rolltui_widget_kinds.c`)
  // draws and edits. `Input::handle()` (public) lets that plugin's `ctx` BORROW the
  // `RolltuiInput*` this map owns, so both readings are one object.
  std::map<std::string, Input> inputs_;
  // Phase 17 m1c: the same trick, now for `transcript`/`menu` — `Transcript::handle()`/
  // `Menu::handle()` (public, added 2026-09-05) are the accessors `Input::handle()` already
  // had, which is exactly what let `input` port while these two did not until now.
  // `transcript()`/`menu()` hand back a LIVE reference into these maps; the pure-C ctx
  // (`rolltui/c/rolltui_widget_kinds.c`) BORROWS the same handle, so a host driving one
  // directly (`transcript().handle(...)`, `menu().handle(...)`, both hosts do) and the window
  // that draws it are one object, never two that could drift apart.
  std::map<std::string, Transcript> transcripts_;
  std::map<std::string, Menu> menus_;
  markdown::Highlighter highlighter_;
  int code_fold_over_lines_ = 0, code_cap_lines_ = 0;
  // THE WIDGET TABLE, THE PER-WINDOW ROUTING TABLE, THE KIND REGISTRY AND THE SCROLLBAR'S
  // TRACKS ARE THE BOUNDARY'S (Phase 15 m5). They were four `std::map`s and two loose
  // scalars here; the one that mattered was `map<string, unique_ptr<Widget>> by_content_`,
  // which is this milestone's named lifetime and is now an explicit table that destroys
  // every widget through the vtable's own `destroy` slot.
  //
  // THE HOST-BINDING SURFACE IS THE BOUNDARY'S TOO, AS OF m6 — `bind_document`'s BORROW
  // half, `bind_rows`/`bind_submit`/`bind_note`, `add_menu`, `set_dir`, and the kind
  // registry's own factory storage. They were six more `std::map`s (one, `factories_`,
  // duplicating the layout registry's own host-kind table — `registered()` now asks that
  // table directly) and a `std::string dir_`. A `std::function` crosses as {function
  // pointer, `void* ctx`, an optional `void (*free_ctx)(void*)`}, the same shape
  // `Effects.cpp`'s `register_effect_kind` already uses for a host's effect kinds. Phase 15
  // m6 had this C++ boundary reached through `handle()` rather than a `friend class
  // WidgetBase`; Phase 17 m1c removed `WidgetBase` itself (transcript/menu's C++ `Widget`
  // subclass, `unbound()` and all) once neither kind needed it. `owned_documents_` and the
  // three `help_*` members above stay C++: neither owns a `std::function`, and moving them
  // would buy nothing (this file's own `rolltui_windows_bind_document` doc comment says why
  // for the former; `rolltui/c/rolltui_widgets.h`'s header comment says why for the latter).
  std::unique_ptr<RolltuiWindows, Handle> w_{rolltui_windows_new()};
};

}  // namespace rolltui
