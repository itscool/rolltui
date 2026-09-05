// rolltui/Widgets.cpp — see Widgets.hpp. Every concrete widget lives here: a host
// reaches them through Windows, so the set of kinds is one table (Layout.hpp) and one
// factory (widget_for), never a chain of names in an application.
#include "rolltui/Widgets.hpp"

#include "rolltui/Scratch.hpp"

#include <algorithm>
#include <filesystem>
#include <variant>

#include "rolltui/Unicode.hpp"
#include "rolltui/Wrap.hpp"

namespace rolltui {

namespace {

// WHAT THE VTABLE CANNOT CARRY, and why it is a per-call context rather than a slot: a
// widget draws with a `Frame&` and a `const Theme&`, and a `Theme` is a C++ object owning a C
// effect map. Handing one across for every widget of every frame would be a view boundary
// onto a module that has not ported — the trade m2 declined for `EffectSpec`. So `draw`
// crosses with the frame handle the C needs and the pair travels beside it, set by
// `Windows::draw` for the length of one call and restored after (a nested draw is real: the
// error panel is drawn from inside one).
struct DrawCtx {
  Frame* frame = nullptr;
  const Theme* theme = nullptr;
};
DrawCtx& draw_ctx() {
  static thread_local DrawCtx c;
  return c;
}

// The two event conversions the adapter needs. A `PasteEvent`'s text is a BORROW for the
// call, which is the same window the decoder's own envelope states.
RolltuiEvent c_event_of(const Event& e) {
  RolltuiEvent ev{};
  if (const KeyEvent* k = std::get_if<KeyEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_KEY;
    ev.key = chord_of(*k);
  } else if (const MouseEvent* m = std::get_if<MouseEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_MOUSE;
    ev.mouse = *m;
  } else if (const PasteEvent* p = std::get_if<PasteEvent>(&e)) {
    ev.kind = ROLLTUI_EVENT_PASTE;
    ev.text = p->text.data();
    ev.text_len = p->text.size();
  }
  return ev;
}

// Declared here and defined with the vtable adapter below: `register_kind` needs it and sits
// above the concrete kinds it wraps.
RolltuiWidget as_widget(std::unique_ptr<Widget> w);

Event event_of(const RolltuiEvent& e) {
  if (e.kind == ROLLTUI_EVENT_MOUSE) return e.mouse;
  if (e.kind == ROLLTUI_EVENT_PASTE) return PasteEvent{std::string(e.text ? e.text : "", e.text_len)};
  return key_event_of(e.key);
}

}  // namespace

std::string WindowsReport::summary() const {
  if (bad_values.empty()) return {};
  std::string s = bad_values.front();
  if (bad_values.size() > 1) s += " (+" + std::to_string(bad_values.size() - 1) + " more)";
  return s;
}

Rect content_rect(const ResolvedNode& rn) {
  Rect r;
  rolltui_content_rect(&rn, &r);
  return r;
}

// ---- the scrollbar's geometry (Widgets.hpp) -----------------------------------------

bool scroll_thumb(const Widget::ScrollExtent& e, int track, ScrollThumb& out) {
  RolltuiScrollThumb t;
  if (!rolltui_scroll_thumb(&e, track, &t)) return false;
  out.offset = t.offset;
  out.length = t.length;
  return true;
}

std::size_t scroll_first_for_cell(const Widget::ScrollExtent& e, int track, int cell) {
  return rolltui_scroll_first_for_cell(&e, track, cell);
}

int draw_scrolled_text(const ResolvedNode& rn, Frame& f, const Theme& theme, std::string_view text, int top,
                       bool ambiguous_wide) {
  const Rect r = content_rect(rn);
  WrapOptions wo;
  wo.ambiguous_wide = ambiguous_wide;
  const WrapLines lines = wrap(text, std::max(r.w, 1), wo);
  const int total = static_cast<int>(lines.size());
  if (r.w <= 0 || r.h <= 0) return total;
  int y = r.y;
  for (std::size_t i = static_cast<std::size_t>(std::max(top, 0)); i < lines.size() && y < r.y + r.h; ++i)
    f.put_text(r.x + lines[i].indent, y++, lines[i].text, theme.style(Role::text), std::max(r.w - lines[i].indent, 0),
               ambiguous_wide);
  const int below = total - std::max(top, 0) - r.h;
  const std::string marker = scroll_marker_text(below > 0 ? static_cast<std::size_t>(below) : 0, r.w, ambiguous_wide);
  if (!marker.empty()) {
    const int mw = unicode::display_width(marker, ambiguous_wide);
    f.put_text(r.x + std::max(r.w - mw, 0), r.y + r.h - 1, marker, theme.style(Role::scroll_marker), mw, ambiguous_wide);
  }
  return total;
}

std::string help_document(const Bindings& b, std::string_view lead, const std::vector<std::string>& scopes,
                         std::string_view note) {
  std::string out(lead);
  for (const std::string& scope : scopes) {
    out += scope + ":\n";
    for (const std::string& line : help_lines(b, scope)) out += "  " + line + "\n";
  }
  return out + std::string(note);
}

int input_max_rows(int parent_extent, int border_rows) { return std::max(1, parent_extent / 2 - border_rows); }

int input_rows(int text_rows, int end_col, int note_width, int width, int max_rows) {
  max_rows = std::max(max_rows, 1);
  const int rows = std::clamp(text_rows, 1, max_rows);
  if (note_width <= 0) return rows;
  if (rows == 1 && end_col + 2 + note_width <= width) return 1;
  return std::min(rows + 1, max_rows);
}

bool scroll_by_action(const KeyEvent& k, const Bindings& b, int page, int total, int& top) {
  page = std::max(page, 1);
  const std::string_view a = b.action_for(k, "transcript");
  if (a == "transcript.line_up") top -= 1;
  else if (a == "transcript.line_down") top += 1;
  else if (a == "transcript.page_up") top -= page;
  else if (a == "transcript.page_down") top += page;
  else if (a == "transcript.top") top = 0;
  else if (a == "transcript.bottom") top = total;
  else return false;
  top = std::clamp(top, 0, std::max(total - page, 0));
  return true;
}

// ---- the widgets ---------------------------------------------------------------------

// Phase 17 m1c: `WidgetBase`/`TranscriptWidget`/`MenuWidget` — the C++ `Widget` subclasses
// that filled the `transcript`/`menu` kinds through the generic adapter below — are gone.
// Both kinds are pure-C ctxs now (`rolltui/c/rolltui_widget_kinds.c`), BORROWING the
// `RolltuiTranscript*`/`RolltuiMenu*` that `transcripts_`/`menus_` (Widgets.hpp) own, the
// same shape `input` already used. Their construction still crosses here (`transcripts_`/
// `menus_` are C++ maps the boundary never sees), registered in `register_builtin_kinds()`
// below; every per-frame call is the ctx's own.

// ---- Windows -------------------------------------------------------------------------

Windows::Windows() {
  register_builtin_kinds();
  // Phase 17: the pure-C `rows`/`text`/`file`/`help`/`input` plugins read the live bindings
  // straight off `w_` (`rolltui_windows_bindings`), with no C++-side fallback to lean on the
  // way `Windows::bindings()` below has always had — so the boundary needs a valid table from
  // construction, not only from the first `set_env()`. A host that never calls `set_env` (a
  // test constructing `Windows` directly, most directly) must still get `default_bindings()`,
  // exactly what `Windows::bindings()` already promised.
  rolltui_windows_set_bindings(w_.get(), default_bindings().handle());
}
Windows::~Windows() = default;

// What crosses is `&doc->entries`, a real `RolltuiDocument*` — never `doc` itself
// (rolltui_widgets.h's own doc comment says why: a `rolltui::Document` is exactly one
// `RolltuiDocument` member, so this is the actual thing the transcript kind reads, not a
// reinterpretation of an opaque one).
void Windows::bind_document(std::string name, const Document* doc) {
  rolltui_windows_bind_document(w_.get(), name.data(), name.size(), &doc->entries);
}

void Windows::bind_sample_document(std::string name, std::string markdown) {
  DocEntry e;
  e.id = "sample";
  e.text = std::move(markdown);
  Document& d = owned_documents_[name];
  d.entries.clear();
  d.entries.push_back(std::move(e));
  rolltui_windows_bind_document(w_.get(), name.data(), name.size(), &d.entries);
}

// bind_rows/bind_submit/bind_note: Phase 15 m6. The callable is heap-held (never a raw
// `new`) and released the way `Effects.cpp`'s `register_effect_kind` releases a host's
// effect kind — a `unique_ptr` taken back inside the C-invoked deleter, so there is no
// hand-rolled `delete` in this path either. The trampoline checks the callable's own
// truthiness before calling it, the same defensive shape `fn && *fn` had at every call
// site before: a host CAN bind an empty `std::function`, and the boundary must not learn
// what that means, only that the C++ side already decided not to call it.
void Windows::bind_rows(std::string name, RowsFn rows) {
  std::unique_ptr<RowsFn> held = std::make_unique<RowsFn>(std::move(rows));
  rolltui_windows_bind_rows(
      w_.get(), name.data(), name.size(),
      [](void* ctx, RolltuiRows* out) {
        RowsFn& fn = *static_cast<RowsFn*>(ctx);
        if (fn) fn(*out);
      },
      held.get(), [](void* ctx) { const std::unique_ptr<RowsFn> owned(static_cast<RowsFn*>(ctx)); });
  held.release();
}

void Windows::bind_submit(std::string name, SubmitFn submit, OnSubmit on_submit) {
  std::unique_ptr<SubmitFn> held = std::make_unique<SubmitFn>(std::move(submit));
  rolltui_windows_bind_submit(
      w_.get(), name.data(), name.size(),
      [](void* ctx, const char* text, std::size_t len) {
        SubmitFn& fn = *static_cast<SubmitFn*>(ctx);
        if (fn) fn(std::string(text, len));
      },
      held.get(), [](void* ctx) { const std::unique_ptr<SubmitFn> owned(static_cast<SubmitFn*>(ctx)); },
      on_submit == OnSubmit::Keep ? 1 : 0);
  held.release();
}

void Windows::bind_note(std::string name, NoteFn note) {
  std::unique_ptr<NoteFn> held = std::make_unique<NoteFn>(std::move(note));
  rolltui_windows_bind_note(
      w_.get(), name.data(), name.size(),
      [](void* ctx, RolltuiNote* out) {
        NoteFn& fn = *static_cast<NoteFn*>(ctx);
        if (fn) fn(*out);
      },
      held.get(), [](void* ctx) { const std::unique_ptr<NoteFn> owned(static_cast<NoteFn*>(ctx)); });
  held.release();
}

// Phase 17 m1c: PUSHED straight onto every transcript that already exists (`transcripts_`,
// created on demand and never destroyed, so every entry here is one a host or a window has
// asked for), rather than left for each one to PULL on its own next frame. `transcript()`
// below applies the same push to a transcript created AFTER this call, so either order —
// `set_highlighter` before or after a source's first use — ends with the live highlighter on
// every live transcript, which is the same "set at any time, picked up once" contract the
// old per-widget epoch existed to keep; nothing here needs to poll for a change because
// nothing is left to miss one.
void Windows::set_highlighter(markdown::Highlighter h) {
  highlighter_ = std::move(h);
  for (auto& [name, t] : transcripts_) t.set_highlight(highlighter_);
}

void Windows::set_code_fold(int fold_over_lines, int cap_lines) {
  code_fold_over_lines_ = fold_over_lines;
  code_cap_lines_ = cap_lines;
  // Mirrored to the boundary (Phase 17 m1c): the transcript kind is a pure-C plugin now and
  // reads these two ints back through `rolltui_windows_code_fold` at layout time, the same
  // "w keeps its own copy" shape `set_env`'s bindings/env already use.
  const RolltuiCodeFold cf{fold_over_lines, cap_lines};
  rolltui_windows_set_code_fold(w_.get(), &cf);
}

// ONE call registers both halves — the name with the layout vocabulary and the factory
// here — so the vocabulary can never name a kind nothing can build. Rung 1 refuses a
// library name inside register_widget_kind(), which is why the factory is only stored
// after it says yes.
//
// Phase 15 m6: the FACTORY itself is heap-held and handed to the boundary as its own `ctx`
// per kind (the `Effects.cpp`/`bind_rows` shape again), rather than kept in a C++-side
// `factories_` map that a shared, capture-less trampoline looked up by name. That map
// duplicated the layout vocabulary's own host-kind table for no reason beyond existing
// before this boundary did — `registered()` below asks that table directly now.
bool Windows::register_kind(std::string name, Factory factory, SourceRule rule, std::string source_is, std::string* why) {
  if (!factory) {
    if (why) *why = "a widget kind needs a factory";
    return false;
  }
  if (!register_widget_kind(name, rule, std::move(source_is), why)) return false;
  std::unique_ptr<Factory> held = std::make_unique<Factory>(std::move(factory));
  // …and the boundary's half. One call registers the NAME with the layout vocabulary and the
  // FACTORY here, and this is where the host's kind joins the library's own seven in the one
  // table `widget_for` reads (the vtable header's rule 5).
  rolltui_windows_register_kind(
      w_.get(), name.data(), name.size(),
      [](void* c, const char* content, std::size_t n) -> RolltuiWidget {
        Factory& factory = *static_cast<Factory*>(c);
        std::optional<Content> parsed = parse_content(std::string_view(content, n));
        if (!parsed) return RolltuiWidget{};
        std::unique_ptr<Widget> w = factory();
        if (!w) return RolltuiWidget{};
        w->content = *parsed;
        return as_widget(std::move(w));
      },
      held.get(), [](void* c) { const std::unique_ptr<Factory> owned(static_cast<Factory*>(c)); });
  held.release();
  return true;
}

Widget* Windows::registered(std::string_view kind, std::string_view source) {
  // "Does a HOST own this kind" is exactly what the layout vocabulary's rung 2 already
  // answers (Layout.cpp's `rolltui_widget_kind_resolve`) — asking it directly is what let
  // the redundant `factories_` map above go. A library kind (rung 1) answers "no": this
  // accessor has always been for a host's own registered widget, never `transcript()`'s.
  unsigned char ordinal = 0;
  if (rolltui_widget_kind_resolve(kind.data(), kind.size(), &ordinal, nullptr, nullptr, nullptr) != ROLLTUI_KIND_HOST)
    return nullptr;
  Content c;
  c.kind = WidgetKind::Registered;
  c.registered_name = std::string(kind);
  c.source = std::string(source);
  return widget_for(content_to_string(c));
}
void Windows::add_menu(std::string name, std::string json_text) {
  rolltui_windows_add_menu(w_.get(), name.data(), name.size(), json_text.data(), json_text.size());
}
void Windows::set_dir(std::string dir) { rolltui_windows_set_dir(w_.get(), dir.data(), dir.size()); }

void Windows::set_help(std::string lead, std::vector<std::string> scopes, std::string note) {
  help_lead_ = std::move(lead);
  help_scopes_ = std::move(scopes);
  help_note_ = std::move(note);
  // Mirrored to the boundary too (Phase 17): `help` is a pure-C plugin now
  // (rolltui/c/rolltui_widget_kinds.c) and reads this back through `rolltui_windows_help_*`
  // rather than through this C++ member — the same "BORROW half stays here, the rest
  // crosses" split `documents_`/`owned_documents_` already makes.
  rolltui_windows_set_help(w_.get(), help_lead_.data(), help_lead_.size(), help_note_.data(), help_note_.size());
  rolltui_windows_clear_help_scopes(w_.get());
  for (const std::string& s : help_scopes_) rolltui_windows_add_help_scope(w_.get(), s.data(), s.size());
}

// One scope, or every one the host set. The lead and the note belong to the whole list,
// so a single-scope window shows neither — it is a column of keys, not a help page.
std::string Windows::help_text(std::string_view scope) const {
  if (scope.empty()) return help_document(bindings(), help_lead_, help_scopes_, help_note_);
  return help_document(bindings(), "", {std::string(scope)}, "");
}

void Windows::set_env(WidgetEnv env) {
  env_ = env;
  // The boundary's half of the environment: the two facts the WINDOW itself draws with (the
  // scrollbar's ambiguous-width thumb) and the clock.
  const RolltuiWidgetEnv e{static_cast<unsigned char>(env_.ambiguous_wide), env_.now_ms};
  rolltui_windows_set_env(w_.get(), &e);
  // Phase 17: the live bindings table crosses too, as a BORROW — every library kind is a
  // pure-C plugin now (m1c closes the set) and reads a chord's action or checks `has()`
  // without knowing `rolltui::Bindings` exists (`rolltui_widget_kinds.c`'s own rule: it names
  // no action). Only HOST-defined `Widget` subclasses still read `bindings()` below directly.
  rolltui_windows_set_bindings(w_.get(), bindings().handle());
}
const Bindings& Windows::bindings() const { return env_.bindings ? *env_.bindings : default_bindings(); }

Widget* Windows::widget_for(const std::string& content) {
  RolltuiWidget* w = rolltui_windows_widget_for(w_.get(), content.data(), content.size());
  return static_cast<Widget*>(w->ctx);
}

// ---- THE GENERIC PLUGIN ADAPTER, for `Widget` subclasses --------------------------------
//
// This is the whole of Phase 15 m5's answer for this module: `Widget`'s virtuals ARE the
// plugin in `rolltui/c/rolltui_widgets.h`, and a widget built from a `Widget` subclass fills
// it exactly the way any other plugin does. Phase 17's widget-kinds port narrowed WHO uses
// this adapter rather than removing it: every library kind (`rows`, `text`, `file`, `help`,
// `input`, `transcript`, `menu`) fills the plugin directly now, in real C
// (`rolltui/c/rolltui_widget_kinds.c`), and none of them come through here anymore. What is
// LEFT is every HOST-defined kind (`Windows::register_kind`'s `Factory =
// std::function<std::unique_ptr<Widget>()>`, which paint's `Canvas` and roll's own
// approval/details widgets use): `Widget` stays a real, working C++ polymorphic base for
// that one audience, and this is the one adapter table every instance of it fills.

namespace {

// OWNERSHIP CROSSES BACK HERE, and it is spelled with a `unique_ptr` rather than a bare
// `delete` — the shape `rolltui_frame_free` already uses, and the one the ownership test
// refuses to let anything else be.
void vt_destroy(void* ctx) { const std::unique_ptr<Widget> owned(static_cast<Widget*>(ctx)); }

void vt_layout(void* ctx, const RolltuiResolvedNode* rn) { static_cast<Widget*>(ctx)->layout(*rn); }

void vt_draw(void* ctx, const RolltuiResolvedNode* rn, RolltuiFrame*) {
  Widget* w = static_cast<Widget*>(ctx);
  DrawCtx& d = draw_ctx();
  w->draw(*rn, *d.frame, *d.theme);
}

int vt_problem(void* ctx, RolltuiStr* out) {
  const std::string why = static_cast<Widget*>(ctx)->problem();
  if (why.empty()) return 0;
  *out = why;
  return 1;
}

int vt_note_at(void* ctx, std::size_t i, RolltuiStr* out) {
  const std::vector<std::string> notes = static_cast<Widget*>(ctx)->notes();
  if (i >= notes.size()) return 0;
  *out = notes[i];
  return 1;
}

int vt_desired_outer(void* ctx, int inner_w, int parent_extent, int border, int* out) {
  const std::optional<int> want = static_cast<Widget*>(ctx)->desired_outer(inner_w, parent_extent, border);
  if (!want) return 0;
  *out = *want;
  return 1;
}

int vt_handle(void* ctx, const RolltuiEvent* e) {
  const Event ev = event_of(*e);
  return static_cast<Widget*>(ctx)->handle(ev) ? 1 : 0;
}

int vt_scroll_extent(void* ctx, unsigned char axis, RolltuiScrollExtent* out) {
  const std::optional<Widget::ScrollExtent> e =
      static_cast<Widget*>(ctx)->scroll_extent(static_cast<Widget::Axis>(axis));
  if (!e) return 0;
  *out = *e;
  return 1;
}

int vt_scroll_to(void* ctx, unsigned char axis, std::size_t first) {
  return static_cast<Widget*>(ctx)->scroll_to(static_cast<Widget::Axis>(axis), first) ? 1 : 0;
}

constexpr RolltuiWidgetPlugin kWidgetPlugin = {
    vt_destroy, vt_layout,        vt_draw,          vt_problem,       vt_note_at,
    vt_desired_outer, vt_handle,  vt_scroll_extent, vt_scroll_to,
};

RolltuiWidget as_widget(std::unique_ptr<Widget> w) {
  RolltuiWidget out{};
  if (!w) return out;
  out.vt = &kWidgetPlugin;
  out.ctx = w.release();
  return out;
}

}  // namespace

// Every library kind and the error panel, registered at construction so `widget_for` has
// ONE path and "a transcript window" is built the way roll's approval modal is (the plugin
// header's rule 5) — `rows`, `text`, `file`, `help`, and the error/panel fallbacks are pure-C
// plugins that need nothing from this C++ object, registered in one call below; `input`,
// `transcript` and `menu` are pure-C plugins too, but each needs a C++ map only `Windows`
// has (`inputs_`/`transcripts_`/`menus_`) to construct from, so their factories stay here.
void Windows::register_builtin_kinds() {
  // The role bytes and the transcript-scope action names the pure-C kinds draw and scroll
  // with — this file names them (rolltui_widget_kinds.h's own rule: that module names
  // neither), computed once from the C++ `Role` enum and handed to the boundary before the
  // kinds that read them are registered.
  RolltuiBuiltinRoles roles{};
  roles.text = static_cast<unsigned char>(Role::text);
  roles.text_muted = static_cast<unsigned char>(Role::text_muted);
  roles.error = static_cast<unsigned char>(Role::error);
  roles.scroll_marker = static_cast<unsigned char>(Role::scroll_marker);
  roles.label = static_cast<unsigned char>(Role::label);
  roles.value = static_cast<unsigned char>(Role::value);
  roles.input_text = static_cast<unsigned char>(Role::input_text);
  roles.input_selection = static_cast<unsigned char>(Role::selection);
  roles.input_placeholder = static_cast<unsigned char>(Role::input_placeholder);
  rolltui_windows_set_builtin_roles(w_.get(), &roles);

  // The identical six strings `rolltui::scroll_by_action` (above, unchanged, still used by
  // two hosts directly) hardcodes — the same one-file duplication `Input.cpp`'s `kActions`
  // already is (rolltui_widget_kinds.h's header comment says why this is not a new one).
  static constexpr RolltuiScrollTextActions kScrollActions{
      "transcript.line_up", "transcript.line_down", "transcript.page_up",
      "transcript.page_down", "transcript.top", "transcript.bottom",
  };
  rolltui_windows_set_scroll_text_actions(w_.get(), &kScrollActions);

  // The eleven action names the transcript kind's own `handle()` needs — the same
  // duplication `Transcript.cpp`'s own (unrelated, still-C++) `kActions` already is, one
  // file over: neither crosses the boundary, because a `std::function` cannot and a bare
  // name table costs nothing to state twice.
  static constexpr RolltuiTranscriptActions kTranscriptActions{
      "transcript.line_up",   "transcript.line_down",  "transcript.page_up",
      "transcript.page_down", "transcript.top",        "transcript.bottom",
      "transcript.find_next", "transcript.find_prev",  "transcript.fold",
      "transcript.copy",      "transcript.clear_selection",
  };
  rolltui_windows_set_transcript_actions(w_.get(), &kTranscriptActions);

  // The seven roles the menu kind's own `draw()` needs — `rolltui_menu_draw` takes them as a
  // per-call parameter rather than storing them (Menu.cpp's own `kRoles`, unrelated), so this
  // is a second copy of the same six-of-seven Role names for the same reason as above.
  static constexpr RolltuiMenuRoles kMenuRoles{
      /*item=*/static_cast<unsigned char>(Role::menu_item),
      /*selected=*/static_cast<unsigned char>(Role::menu_selected),
      /*breadcrumb=*/static_cast<unsigned char>(Role::menu_breadcrumb),
      /*shortcut=*/static_cast<unsigned char>(Role::menu_shortcut),
      /*text_muted=*/static_cast<unsigned char>(Role::text_muted),
      /*warning=*/static_cast<unsigned char>(Role::warning),
      /*scroll_marker=*/static_cast<unsigned char>(Role::scroll_marker),
  };
  rolltui_windows_set_menu_roles(w_.get(), &kMenuRoles);

  // rows, text, file, help, and the error/panel fallbacks: every built-in kind that needs
  // nothing from this C++ object beyond what the boundary already exposes.
  rolltui_widget_kinds_register(w_.get());

  // input: `Windows` OWNS the `Input` here (in `inputs_`, on demand — the same "created once
  // per source" rule `owned_documents_` follows), and the pure-C `input` plugin's `ctx`
  // BORROWS its handle (`Input::handle()` is public precisely so this can happen). Both
  // `windows.input(source)` and the drawn widget then read the one underlying state.
  rolltui_windows_register_kind(
      w_.get(), "input", 5,
      [](void* c, const char* content, std::size_t n) -> RolltuiWidget {
        Windows& self = *static_cast<Windows*>(c);
        std::optional<Content> parsed = parse_content(std::string_view(content, n));
        if (!parsed) return RolltuiWidget{};
        const std::string source = parsed->source.str();
        Input& in = self.inputs_[source];
        void* ctx = rolltui_input_widget_ctx_new(in.handle(), self.w_.get(), source.data(), source.size(),
                                                 rolltui_windows_builtin_roles(self.w_.get()), input_actions());
        RolltuiWidget out{};
        out.vt = rolltui_input_widget_plugin();
        out.ctx = ctx;
        return out;
      },
      this, nullptr);

  // transcript: same shape as `input`, over `transcripts_` — direct map access (never
  // `self.transcript(source)`, which itself constructs this same widget on first use and
  // would recurse back here).
  rolltui_windows_register_kind(
      w_.get(), "transcript", 10,
      [](void* c, const char* content, std::size_t n) -> RolltuiWidget {
        Windows& self = *static_cast<Windows*>(c);
        std::optional<Content> parsed = parse_content(std::string_view(content, n));
        if (!parsed) return RolltuiWidget{};
        const std::string source = parsed->source.str();
        Transcript& t = self.transcripts_[source];
        RolltuiWidget out{};
        out.vt = rolltui_transcript_widget_plugin();
        out.ctx = rolltui_transcript_widget_ctx_new(t.handle(), self.w_.get(), source.data(), source.size());
        return out;
      },
      this, nullptr);

  // menu: same shape, over `menus_`.
  rolltui_windows_register_kind(
      w_.get(), "menu", 4,
      [](void* c, const char* content, std::size_t n) -> RolltuiWidget {
        Windows& self = *static_cast<Windows*>(c);
        std::optional<Content> parsed = parse_content(std::string_view(content, n));
        if (!parsed) return RolltuiWidget{};
        const std::string source = parsed->source.str();
        Menu& m = self.menus_[source];
        RolltuiWidget out{};
        out.vt = rolltui_menu_widget_plugin();
        out.ctx = rolltui_menu_widget_ctx_new(m.handle(), self.w_.get(), source.data(), source.size());
        return out;
      },
      this, nullptr);
}

WindowsReport Windows::sync(const WindowStack& stack) {
  rolltui_windows_sync(w_.get(), stack.handle());
  WindowsReport rep;
  for (std::size_t i = 0; i < rolltui_windows_report_count(w_.get()); ++i) {
    std::size_t n = 0;
    const char* p = rolltui_windows_report_at(w_.get(), i, &n);
    rep.bad_values.emplace_back(p, n);
  }
  return rep;
}

void Windows::autosize(WindowStack& stack, Rect box) { rolltui_windows_autosize(w_.get(), stack.handle(), box); }

void Windows::layout(const WindowStack& stack, Rect box) {
  rolltui_windows_layout(w_.get(), stack.handle(), box);
}

WindowsReport Windows::prepare(WindowStack& stack, Rect box) {
  WindowsReport rep = sync(stack);
  autosize(stack, box);
  layout(stack, box);
  return rep;
}

// THE TWO ROLES THE WINDOW ITSELF DRAWS WITH (the scrollbar's), handed over as bytes.
namespace {
constexpr RolltuiWindowRoles kWindowRoles = {
    /*scrollbar=*/static_cast<unsigned char>(Role::scrollbar),
    /*border=*/static_cast<unsigned char>(Role::border),
    /*border_active=*/static_cast<unsigned char>(Role::border_active),
};
}  // namespace

void Windows::draw(const ResolvedNode& rn, Frame& f, const Theme& theme) {
  // The frame and theme travel in a per-call context rather than through the vtable: a
  // `Theme` is a C++ object with an owned effect map, and handing one across the boundary
  // for every widget of every frame would be a view boundary onto a module that has not
  // ported — exactly the trade m2 declined for `EffectSpec` and wrote down.
  DrawCtx& d = draw_ctx();
  DrawCtx saved = d;
  d = {&f, &theme};
  rolltui_windows_draw(w_.get(), &rn, f.handle(), theme.styles.data(), &kWindowRoles);
  d = saved;
}

bool Windows::handle(std::string_view window, const Event& e) {
  const RolltuiEvent ev = c_event_of(e);
  return rolltui_windows_handle(w_.get(), window.data(), window.size(), &ev) != 0;
}

// Phase 17: the `input` widget's ctx is a plain C struct now (`rolltui_widget_kinds.c`), not
// a `Widget` subclass, so it can no longer be reached through `widget_for` + `static_cast`.
// `rolltui_input_kind_process_event` is the ONE place "handle, then act on Submit" is
// written — the same function the plugin's own `handle` slot calls — so a host asking for
// the action back and the routed-event path can never disagree about what Submit does.
InputAction Windows::input_event(std::string_view source, const Event& e) {
  Input& in = inputs_[std::string(source)];
  const RolltuiEvent ev = c_event_of(e);
  const int action =
      rolltui_input_kind_process_event(in.handle(), w_.get(), source.data(), source.size(), input_actions(), &ev);
  return static_cast<InputAction>(action);
}

// `Windows` OWNS the `Transcript` here (created on demand, exactly like `owned_documents_`),
// so this is valid whether or not any window currently shows it — the drawn widget's ctx
// only BORROWS this same object's handle (see `register_builtin_kinds`'s "transcript"
// factory). A transcript created AFTER `set_highlighter` was called gets the live
// highlighter applied right here, at creation, so the order the two are called in cannot
// matter (`set_highlighter` pushes to every transcript that already exists).
Transcript& Windows::transcript(std::string_view source) {
  const std::string key(source);
  const bool existed = transcripts_.count(key) != 0;
  Transcript& t = transcripts_[key];
  if (!existed && highlighter_) t.set_highlight(highlighter_);
  return t;
}

// `Windows` OWNS the `Input` here (created on demand, exactly like `owned_documents_`), so
// this is valid whether or not any window currently shows it — the drawn widget's ctx only
// BORROWS this same object's handle (see `register_builtin_kinds`'s "input" factory).
Input& Windows::input(std::string_view source) { return inputs_[std::string(source)]; }

void Windows::set_input_min_outer(std::string_view source, int rows) {
  // The widget, not the `Input` — `min_outer` is per-window sizing state the plugin's ctx
  // keeps, not part of the edited text the `inputs_` map owns. Created on demand, like every
  // other `widget_for` call: a host may set the floor before any window has shown this input.
  const std::string content = "input:" + std::string(source);
  RolltuiWidget* w = rolltui_windows_widget_for(w_.get(), content.data(), content.size());
  if (w && w->ctx) rolltui_input_widget_ctx_set_min_outer(w->ctx, rows);
}

// `Windows` OWNS the `Menu` here (`menus_`, created on demand), so this is valid whether or
// not any window currently shows it — exactly `transcript()`'s shape. What `Menu` alone
// cannot do is re-resolve its FILE: that state (loaded/stamp/origin/problem) lives in the
// widget's own ctx now (Phase 17 m1c), so this forces a refresh through `widget_for` — which
// builds the ctx on first call via `register_builtin_kinds`'s "menu" factory, a DIRECT
// `menus_[source]` access that never calls back into this method (calling `self.menu(source)`
// from that factory would recurse: `menu()` calls `widget_for`, whose first call IS the
// factory). A host asking for `windows.menu("main")` before any window has shown it — every
// `layout_test.cpp` case does — still gets the file read right now, not at the next draw.
Menu& Windows::menu(std::string_view source) {
  const std::string content = "menu:" + std::string(source);
  RolltuiWidget* wd = rolltui_windows_widget_for(w_.get(), content.data(), content.size());
  if (wd && wd->ctx) rolltui_menu_widget_ctx_refresh(wd->ctx);
  return menus_[std::string(source)];
}

std::string Windows::menu_origin(std::string_view source) {
  const std::string content = "menu:" + std::string(source);
  RolltuiWidget* wd = rolltui_windows_widget_for(w_.get(), content.data(), content.size());
  if (!wd || !wd->ctx) return {};
  std::size_t len = 0;
  const char* p = rolltui_menu_widget_ctx_origin(wd->ctx, &len);
  return std::string(p, len);
}

std::vector<std::string> Windows::menu_names() const {
  std::vector<std::string> out;
  auto add = [&out](std::string name) {
    if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
  };
  std::size_t dir_len = 0;
  const char* dir_p = rolltui_windows_dir(w_.get(), &dir_len);
  if (const std::string_view d(dir_p, dir_len); !d.empty()) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(std::string(d) + "/menus", ec))
      if (e.path().extension() == ".json") add(e.path().stem().string());
  }
  for (std::size_t i = 0; i < rolltui_windows_host_menu_count(w_.get()); ++i) {
    std::size_t n = 0;
    const char* p = rolltui_windows_host_menu_name_at(w_.get(), i, &n);
    add(std::string(p, n));
  }
  for (std::string_view name : shipped_menu_names()) add(std::string(name));
  std::sort(out.begin(), out.end());
  return out;
}

// Phase 17: `at()`'s `static_cast<Widget*>` is only valid for a window whose widget IS a
// `Widget` subclass — true of every HOST-defined kind now, false of every library kind's
// pure-C ctx struct (`rows`/`text`/`file`/`help`/`input`/`transcript`/`menu` alike, m1c
// closing the set). It is used only below, and only AFTER a kind check (`content_at`, which
// never dereferences a widget at all) has already confirmed which of those two worlds a
// window's content is in.
Widget* Windows::at(std::string_view window) const {
  RolltuiWidget* w = rolltui_windows_at(w_.get(), window.data(), window.size());
  return w ? static_cast<Widget*>(w->ctx) : nullptr;
}

// Reads the WINDOW's content string directly off the boundary rather than through `at()`'s
// `Widget*` — the one accessor here that must work for every kind alike, ported or not.
std::optional<Content> Windows::content_at(std::string_view window) const {
  std::size_t len = 0;
  const char* p = rolltui_windows_content_at(w_.get(), window.data(), window.size(), &len);
  if (!p) return std::nullopt;
  return parse_content(std::string_view(p, len));
}

// `transcript`'s ctx is not a `Widget` subclass (Phase 17 m1c), so this reads `Windows`' own
// `transcripts_` map instead of `at()` — the SAME object the drawn widget's ctx borrows.
Transcript* Windows::transcript_at(std::string_view window) const {
  const std::optional<Content> c = content_at(window);
  if (!c || c->kind != WidgetKind::Transcript) return nullptr;
  const auto it = transcripts_.find(c->source.str());
  return it != transcripts_.end() ? const_cast<Transcript*>(&it->second) : nullptr;
}

// `input`'s ctx is not a `Widget` subclass (Phase 17), so this reads `Windows`' own `inputs_`
// map instead of `at()` — the SAME object the drawn widget's ctx borrows.
Input* Windows::input_at(std::string_view window) const {
  const std::optional<Content> c = content_at(window);
  if (!c || c->kind != WidgetKind::Input) return nullptr;
  const auto it = inputs_.find(c->source.str());
  return it != inputs_.end() ? const_cast<Input*>(&it->second) : nullptr;
}

// `menu`'s ctx is not a `Widget` subclass either (Phase 17 m1c) — reads `menus_` directly,
// same shape as `input_at`/`transcript_at`.
Menu* Windows::menu_at(std::string_view window) const {
  const std::optional<Content> c = content_at(window);
  if (!c || c->kind != WidgetKind::Menu) return nullptr;
  const auto it = menus_.find(c->source.str());
  return it != menus_.end() ? const_cast<Menu*>(&it->second) : nullptr;
}

}  // namespace rolltui
