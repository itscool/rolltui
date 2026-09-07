//
// transcript_test.cpp — the transcript widget's rules (Transcript.hpp): the layout
// cache re-lays only what changed; the scroll anchor survives a re-wrap; follow mode
// and the "▼ N more" marker; folding by click and Ctrl-O; hit-testing, drag selection
// in logical coordinates (no wrap artefacts in the copy, spanning entries, word and
// line clicks), edge auto-scroll; OSC 8 hyperlinks from parsed URLs; chrome never
// copies. No terminal: every check is on a Frame or on the widget's state.
//
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// the C API, through the umbrella alone. What this file gained is the FIXTURE
// below — a `Frame` with the five accessors this suite reads, a `Theme` that is a styles array
// with a name, and a `Role` expanded from the library's own list. That fixture is deliberately
// private and deliberately small. The standing condition on a mirror like it is that it may
// hold a SHAPE and never a rule or a word — every rule here (the wrap, the marker, the action
// names, the role names) is a call, not a copy.
#include "rolltui/rolltui.h"
#include "md_test_helpers.hpp"
#include "rolltui_test.hpp"
#include "rolltui/c/rolltui_render.h"  // INTERNAL: this test opts in
#include "rolltui/c/rolltui_screen.h"  // INTERNAL: this test opts in
#include "rolltui/c/rolltui_theme.h"  // INTERNAL: this test opts in
#include "rolltui/c/rolltui_transcript.h"  // INTERNAL: this test opts in

using namespace rolltui_test;

namespace {

// ---- the fixture: the C++ shapes this suite reads, over the C ---------------------------

// ONE draw scratch for this binary (`Screen.cpp` kept one per thread; a test is single-
// threaded, so one is the whole of it). CALLER-FILLED working memory, CLAUDE.md's strategy 3.
RolltuiDrawScratch* draw_scratch() {
  static RolltuiDrawScratch* d = rolltui_draw_scratch_new();
  return d;
}
RolltuiUnicodeScratch* u_scratch() {
  static RolltuiUnicodeScratch* u = rolltui_u_scratch_new();
  return u;
}


enum class Role : unsigned char {
#define ROLLTUI_TEST_ROLE_(lower, UPPER) lower,
  ROLLTUI_ROLE_LIST(ROLLTUI_TEST_ROLE_)
#undef ROLLTUI_TEST_ROLE_
  count_
};
constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::count_);
// `RolltuiDocEntry::role`/`prefix_role` are typed `rolltui::Role` in C++ and `unsigned char`
// in C (rolltui_document.h), so a consumer needs this one cast. A KNOWN WART: that field
// names a C++ type for a vocabulary that is C, and every host hits it.
constexpr rolltui::Role as_role(Role r) { return static_cast<rolltui::Role>(r); }
using Color = RolltuiStyleColor;
using Style = RolltuiStyle;
using Rect = RolltuiRect;
using KeyEvent = RolltuiChord;
using MouseEvent = RolltuiMouseEvent;

// A frame, OWNED, with exactly the five accessors this suite reads. Copy and assignment are
// kept because the checks build one, keep a copy, redraw and compare.
class Frame {
 public:
  Frame() : f_(rolltui_frame_new(0, 0, RolltuiStyle{})) {}
  Frame(int w, int h, const Style& fill = {}) : f_(rolltui_frame_new(w, h, fill)) {}
  Frame(const Frame& o) : f_(rolltui_frame_clone(o.f_)) {}
  Frame& operator=(const Frame& o) {
    if (this != &o) {
      rolltui_frame_free(f_);
      f_ = rolltui_frame_clone(o.f_);
    }
    return *this;
  }
  Frame(Frame&& o) noexcept : f_(o.f_) { o.f_ = nullptr; }
  Frame& operator=(Frame&& o) noexcept {
    if (this != &o) {
      rolltui_frame_free(f_);
      f_ = o.f_;
      o.f_ = nullptr;
    }
    return *this;
  }
  ~Frame() { rolltui_frame_free(f_); }
  int width() const { return rolltui_frame_width(f_); }
  int height() const { return rolltui_frame_height(f_); }
  RolltuiCell at(int x, int y) const {
    RolltuiCell c;
    rolltui_frame_cell(f_, x, y, &c);
    return c;
  }
  // A BORROW from the frame, valid until that cell is written again — the accessor to use,
  // because a `Cell`'s own inline bytes die with the returned copy.
  std::string_view glyph(int x, int y) const {
    std::size_t n = 0;
    const char* p = rolltui_frame_glyph(f_, x, y, &n);
    return std::string_view(p, n);
  }
  std::string_view link(unsigned int id) const {
    std::size_t n = 0;
    const char* p = rolltui_frame_link(f_, id, &n);
    return std::string_view(p ? p : "", n);
  }
  RolltuiFrame* handle() const { return f_; }
  int put_text(int x, int y, std::string_view utf8, const Style& style, int max_cells, bool ambiguous_wide = false,
               unsigned int link = 0) {
    return rolltui_frame_put_text(f_, draw_scratch(), x, y, utf8.data(), utf8.size(), style, max_cells,
                                  ambiguous_wide ? 1 : 0, link);
  }
  unsigned int link_id(std::string_view url) { return rolltui_frame_link_id(f_, url.data(), url.size()); }

 private:
  RolltuiFrame* f_ = nullptr;
};

// The three free functions this suite calls that hand back an unbounded string: the C shape is
// APPEND-to-a-caller's-RolltuiStr (`rolltui/rolltui.h` rule 3(b)), and these are that with the
// string moved out, because a check compares one value and throws it away.
std::string render_full(const Frame& f, unsigned char depth) {
  RolltuiStr out{};
  rolltui_render_full(f.handle(), depth, &out);
  std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}
std::string render_diff(const Frame* prev, const Frame& next, unsigned char depth) {
  RolltuiStr out{};
  rolltui_render_diff(prev ? prev->handle() : nullptr, next.handle(), depth, &out);
  std::string s(out.p ? out.p : "", out.n);
  rolltui_str_free(&out);
  return s;
}
namespace unicode {
inline int display_width(std::string_view s, bool ambiguous_wide) {
  return rolltui_u_display_width(u_scratch(), s.data(), s.size(), ambiguous_wide ? 1 : 0);
}
}  // namespace unicode

// The style table and the name this suite reads, filled from the C built-ins. `.effects` and
// `.meta` are not carried: no theme text below has either, and a shape that carries what it
// does not read is the start of a second `Theme`.
struct Theme {
  std::string name;
  std::array<RolltuiStyle, kRoleCount> styles{};
  const RolltuiStyle& style(Role r) const {
    return *rolltui_theme_style(styles.data(), styles.size(), static_cast<unsigned char>(r));
  }
};
const Theme* builtin_theme(std::string_view name) {
  static const std::vector<std::pair<std::string, Theme>> cache = [] {
    std::vector<std::pair<std::string, Theme>> v;
    const std::size_t n = rolltui_theme_builtin_count();
    v.reserve(n);  // pointer stability: this hands back &t into the vector
    for (std::size_t i = 0; i < n; ++i) {
      const char* nm = rolltui_theme_builtin_name(i);
      Theme t;
      t.name = nm;
      if (RolltuiEffectMap* m = rolltui_theme_builtin_fill(nm, std::strlen(nm), t.styles.data(), t.styles.size()))
        rolltui_effect_map_free(m);
      v.emplace_back(nm, t);
    }
    return v;
  }();
  for (const auto& [n, t] : cache)
    if (n == name) return &t;
  return nullptr;
}

std::string scroll_marker_text(std::size_t hidden, int width, bool ambiguous_wide = false) {
  char buf[ROLLTUI_MARKER_MAX];
  return std::string(buf, rolltui_scroll_marker_text(hidden, width, ambiguous_wide ? 1 : 0, buf, sizeof buf));
}


RolltuiDocEntry user(const char* id, std::string text) {
  RolltuiDocEntry e;
  e.id = id;
  set_str(e.text, std::move(text));
  e.markdown = false;
  e.prefix = "> ";
  e.prefix_role = as_role(Role::prompt);
  return e;
}
RolltuiDocEntry md(const char* id, std::string text) {
  RolltuiDocEntry e;
  e.id = id;
  set_str(e.text, std::move(text));
  e.markdown = true;
  return e;
}
RolltuiDocEntry verbatim(const char* id, std::string text) {
  RolltuiDocEntry e;
  e.id = id;
  set_str(e.text, std::move(text));
  e.markdown = false;
  return e;
}
RolltuiDocEntry tool(const char* id, std::string summary, std::string text) {
  RolltuiDocEntry e = verbatim(id, std::move(text));
  e.foldable = true;
  set_str(e.summary, std::move(summary));
  e.folded = true;
  return e;
}

MouseEvent mouse(MouseEvent::Kind k, int x, int y, bool shift = false) {
  MouseEvent m;
  m.kind = k;
  m.x = x;
  m.y = y;
  m.button = 1;
  m.shift = shift;
  return m;
}
// `KeyEvent` IS `RolltuiChord` here — the C's own struct, so there is no conversion at all
// and the `raw` field that used to need clearing before every comparison does not exist.
KeyEvent key(unsigned char k) { KeyEvent e{}; e.key = k; return e; }
KeyEvent ctrl(char c) { KeyEvent e{}; e.key = ROLLTUI_KEY_CHAR; e.ch = static_cast<RolltuiCodepoint>(c); e.ctrl = 1; return e; }
KeyEvent alt(char c) { KeyEvent e{}; e.key = ROLLTUI_KEY_CHAR; e.ch = static_cast<RolltuiCodepoint>(c); e.alt = 1; return e; }

std::string row_text(const Frame& f, int y) {
  std::string s;
  for (int x = 0; x < f.width(); ++x)
    if (!f.at(x, y).continuation) s += f.glyph(x, y);
  std::size_t end = s.find_last_not_of(' ');
  return end == std::string::npos ? "" : s.substr(0, end + 1);
}

std::string lines(int n) {
  std::string s;
  for (int i = 1; i <= n; ++i) s += "line " + std::to_string(i) + (i < n ? "\n" : "");
  return s;
}

// ---- what a C++ caller of the C transcript writes for itself -------------------------------
// The clipboard trampoline in place of a `std::function`, the KeyEvent/MouseEvent ->
// RolltuiEvent conversion, and one free function per method. A suite that calls the C API
// directly has nowhere else for this mapping to live.
//
// IT NAMES NO ROLE AND NO ACTION. The six roles a draw needs are the transcript's own
// defaults, installed by `rolltui_transcript_new`; the eleven action names are the C's. A copy
// of either here would be the SECOND copy, which is what moved them into the library.

// OWNED, through the same "unique_ptr-shaped" deleter every owned handle in this library
// uses (Frame::Handle, the deleted Transcript::Handle) — here a plain struct instead of a
// unique_ptr, since every operation below is a free function taking the handle explicitly
// rather than a method needing `this`. `Transcript()`'s constructor, kept whole.
struct TranscriptHandle {
  RolltuiTranscript* p;
  TranscriptHandle() : p(rolltui_transcript_new()) {}
  ~TranscriptHandle() { rolltui_transcript_free(p); }
  TranscriptHandle(const TranscriptHandle&) = delete;
  TranscriptHandle& operator=(const TranscriptHandle&) = delete;
  operator RolltuiTranscript*() const { return p; }
};

// THE HOST'S CLIPBOARD, as a function pointer and a context — in place of `on_copy`'s
// `std::function`. `count` is optional: not every scope that copies also counts copies.
struct CopySink {
  std::string* text;
  int* count = nullptr;
};
void copy_trampoline(void* ctx, const char* text, std::size_t len) {
  CopySink& s = *static_cast<CopySink*>(ctx);
  *s.text = std::string(text, len);
  if (s.count) ++*s.count;
}

// `handle()`'s KeyEvent/MouseEvent -> RolltuiEvent conversion, done inline in the shim.
RolltuiEvent to_event(const KeyEvent& k) {
  RolltuiEvent ev{};
  ev.kind = ROLLTUI_EVENT_KEY;
  ev.key = k;  // already a chord
  return ev;
}
RolltuiEvent to_event(const MouseEvent& m) {
  RolltuiEvent ev{};
  ev.kind = ROLLTUI_EVENT_MOUSE;
  ev.mouse = m;
  return ev;
}

// ---- the Transcript operations this suite uses, each a shim method's body verbatim,
// taking the handle as an explicit first argument in place of `this`. ----------------
void layout(RolltuiTranscript* t, const RolltuiDocument& doc, RolltuiRect area,
            const RolltuiTranscriptOptions& opt) {
  rolltui_transcript_layout(t, &doc, area, &opt);
}
void draw(const RolltuiTranscript* t, Frame& f, const Theme& theme, RolltuiDrawScratch* scratch) {
  rolltui_transcript_draw(t, f.handle(), scratch, theme.styles.data());
}
bool handle(RolltuiTranscript* t, const KeyEvent& k, const RolltuiDocument& doc, std::uint64_t now_ms) {
  RolltuiEvent ev = to_event(k);
  return rolltui_transcript_handle(t, &ev, &doc, now_ms, rolltui_bindings_default(rolltui_test::test_context()), rolltui_transcript_default_actions()) != 0;
}
bool handle(RolltuiTranscript* t, const MouseEvent& m, const RolltuiDocument& doc, std::uint64_t now_ms) {
  RolltuiEvent ev = to_event(m);
  return rolltui_transcript_handle(t, &ev, &doc, now_ms, rolltui_bindings_default(rolltui_test::test_context()), rolltui_transcript_default_actions()) != 0;
}
std::string selected_text(const RolltuiTranscript* t) {
  RolltuiStr s;
  rolltui_transcript_selected_text(t, &s);
  return str_of(s);
}
std::size_t top_line(const RolltuiTranscript* t) { return rolltui_transcript_top_line(t); }
RolltuiScrollAnchor scroll(const RolltuiTranscript* t) {
  RolltuiScrollAnchor a;
  rolltui_transcript_scroll(t, &a);
  return a;
}
std::size_t match_count(const RolltuiTranscript* t) { return rolltui_transcript_match_count(t); }
bool set_query(RolltuiTranscript* t, std::string_view q) {
  return rolltui_transcript_set_query(t, q.data(), q.size()) != 0;
}
RolltuiTranscriptStats stats(const RolltuiTranscript* t) {
  RolltuiTranscriptStats s;
  rolltui_transcript_stats(t, &s);
  return s;
}
const RolltuiEntryLayout* layout_of(const RolltuiTranscript* t, std::size_t entry) {
  return rolltui_transcript_layout_of(t, entry);
}
std::size_t total_lines(const RolltuiTranscript* t) { return rolltui_transcript_total_lines(t); }
RolltuiSelection selection(const RolltuiTranscript* t) {
  RolltuiSelection s;
  rolltui_transcript_selection(t, &s);
  return s;
}
std::optional<RolltuiTextPos> hit(const RolltuiTranscript* t, int x, int y) {
  RolltuiTextPos p;
  if (!rolltui_transcript_hit(t, x, y, &p)) return std::nullopt;
  return p;
}
void scroll_to_top(RolltuiTranscript* t) { rolltui_transcript_scroll_to_top(t); }
bool wants_tick(const RolltuiTranscript* t) { return rolltui_transcript_wants_tick(t) != 0; }
void scroll_by(RolltuiTranscript* t, long n) { rolltui_transcript_scroll_by(t, n); }
std::size_t lines_below(const RolltuiTranscript* t) { return rolltui_transcript_lines_below(t); }
std::size_t current_match_number(const RolltuiTranscript* t) {
  return rolltui_transcript_current_match_number(t);
}
void tick(RolltuiTranscript* t) { rolltui_transcript_tick(t); }
std::optional<RolltuiFindMatch> current_match(const RolltuiTranscript* t) {
  RolltuiFindMatch m;
  if (!rolltui_transcript_current_match(t, &m)) return std::nullopt;
  return m;
}
RolltuiRect text_area(const RolltuiTranscript* t) {
  RolltuiRect r;
  rolltui_transcript_text_area(t, &r);
  return r;
}
void set_code_folded(RolltuiTranscript* t, std::string_view id, std::size_t block, bool folded) {
  rolltui_transcript_set_code_folded(t, id.data(), id.size(), block, folded);
}
void scroll_page(RolltuiTranscript* t, int direction) { rolltui_transcript_scroll_page(t, direction); }
bool is_folded(const RolltuiTranscript* t, const RolltuiDocEntry& e) {
  return rolltui_transcript_is_folded(t, &e) != 0;
}
bool find_next(RolltuiTranscript* t) { return rolltui_transcript_find_next(t) != 0; }
void select(RolltuiTranscript* t, RolltuiTextPos anchor, RolltuiTextPos head) {
  rolltui_transcript_select(t, anchor, head);
}
void toggle_fold(RolltuiTranscript* t, const RolltuiDocEntry& e) {
  const std::string_view id = view_of(e.id);
  rolltui_transcript_set_folded(t, id.data(), id.size(), !is_folded(t, e));
}
void set_code_uncapped(RolltuiTranscript* t, std::string_view id, std::size_t block, bool uncapped) {
  rolltui_transcript_set_code_uncapped(t, id.data(), id.size(), block, uncapped);
}
void scroll_to_bottom(RolltuiTranscript* t) { rolltui_transcript_scroll_to_bottom(t); }
std::string_view query(const RolltuiTranscript* t) {
  std::size_t n = 0;
  const char* p = rolltui_transcript_query(t, &n);
  return std::string_view(p, n);
}
RolltuiFindMatch match_at(const RolltuiTranscript* t, std::size_t i) {
  RolltuiFindMatch m;
  rolltui_transcript_match_at(t, i, &m);
  return m;
}
bool find_prev(RolltuiTranscript* t) { return rolltui_transcript_find_prev(t) != 0; }

}  // namespace

int main() {
  const Theme& theme = *builtin_theme("default-dark");
  RolltuiTranscriptOptions opt;
  opt.gap = 1;
  // OWNED scratch for every draw() below (Transcript.cpp kept one per thread via
  // ThreadHandle; a single-threaded test binary keeps the one explicit pair instead).
  RolltuiDrawScratch* scratch = rolltui_draw_scratch_new();

  // ---- the layout cache: only what changed is re-laid ----
  {
    RolltuiDocument doc;
    doc.push_back(user("u1", "hello world"));
    doc.push_back(md("a1", "# Plan\n\nA paragraph that is long enough to wrap at forty cells, with a [link](https://example.com/x) in it.\n\n```sh\ncmake --build build\n```\n\n- one\n- two"));
    doc.push_back(verbatim("n1", "[roll] a note"));
    doc.push_back(tool("t1", "read_file README.md", "line one\nline two\nline three"));
    doc.push_back(md("a2", "streaming"));
    TranscriptHandle tr;
    layout(tr, doc, {0, 0, 40, 10}, opt);
    check(stats(tr).entries_relaid == 5, "first layout lays out every entry (" + std::to_string(stats(tr).entries_relaid) + ")");
    layout(tr, doc, {0, 0, 40, 10}, opt);
    check(stats(tr).entries_relaid == 0, "an identical frame re-lays nothing");
    doc[4].text += " more text";
    doc[4].version++;
    layout(tr, doc, {0, 0, 40, 10}, opt);
    check(stats(tr).entries_relaid == 1, "a streaming chunk (version bump) re-lays exactly the streaming entry");
    doc[4].text += " again";  // no version bump: the host's bug, and the rule
    layout(tr, doc, {0, 0, 40, 10}, opt);
    check(stats(tr).entries_relaid == 0, "without a version bump the cache is trusted (the key, never the event)");
    doc[4].version++;
    layout(tr, doc, {0, 0, 60, 10}, opt);
    check(stats(tr).entries_relaid == 5, "a new width re-lays everything once");
    layout(tr, doc, {0, 0, 60, 10}, opt);
    check(stats(tr).entries_relaid == 0, "and then nothing");
    toggle_fold(tr, doc[3]);
    layout(tr, doc, {0, 0, 60, 10}, opt);
    check(stats(tr).entries_relaid == 1, "toggling a fold re-lays that entry only");
    check(stats(tr).cache_size == 5, "the cache holds one layout per entry (" + std::to_string(stats(tr).cache_size) + ")");
    // Sweep: a document whose ids all change leaves the old ids behind until the
    // cache outgrows it.
    RolltuiDocument other;
    for (int i = 0; i < 100; ++i) other.push_back(verbatim(("x" + std::to_string(i)).c_str(), "x"));
    layout(tr, other, {0, 0, 60, 10}, opt);
    RolltuiDocument tiny;
    tiny.push_back(verbatim("only", "y"));
    layout(tr, tiny, {0, 0, 60, 10}, opt);
    check(stats(tr).cache_size <= 1 + 32 + 2, "stale ids are swept when the cache outgrows the document (" + std::to_string(stats(tr).cache_size) + ")");
  }

  // ---- the anchor survives a re-wrap ----
  {
    RolltuiDocument doc;
    doc.push_back(user("u", "first"));
    doc.push_back(md("a", "Alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi omega, and once more: alpha beta gamma delta epsilon zeta eta theta iota kappa lambda."));
    doc.push_back(verbatim("n", lines(12)));
    TranscriptHandle tr;
    layout(tr, doc, {0, 0, 30, 6}, opt);
    check(scroll(tr).follow && top_line(tr) == total_lines(tr) - 6, "a fresh transcript follows the bottom");
    scroll_to_top(tr);
    scroll_by(tr, 3);  // into entry 1: its block is 1 gap line + wrapped lines, so line 2 = its second content line
    check(scroll(tr).entry == 1 && scroll(tr).line == 2 && !scroll(tr).follow,
          "the anchor is (entry, line within block): entry 1 line 2 after Home + 3");
    Frame f30(30, 6);
    draw(tr, f30, theme, scratch);
    const std::string top30 = row_text(f30, 0);
    const std::size_t total30 = total_lines(tr);
    layout(tr, doc, {0, 0, 60, 6}, opt);
    check(total_lines(tr) < total30, "a wider layout has fewer lines");
    check(scroll(tr).entry == 1 && scroll(tr).line == 2, "the same entry and line stay at the top after the re-wrap");
    Frame f(60, 6);
    draw(tr, f, theme, scratch);
    auto top_hit = hit(tr, 0, 0);
    check(top_hit && top_hit->entry == 1 && row_text(f, 0) != top30 && row_text(f, 0).find("Alpha") == std::string::npos,
          "the top row is entry 1's second content line at the new width (re-wrapped, not the first): [" + row_text(f, 0) + "]");
    // A line index beyond the entry's new (shorter) block clamps rather than jumping.
    layout(tr, doc, {0, 0, 30, 6}, opt);
    scroll_to_top(tr);
    scroll_by(tr, 8);
    const RolltuiScrollAnchor a = scroll(tr);
    layout(tr, doc, {0, 0, 200, 6}, opt);
    check(scroll(tr).entry == a.entry && scroll(tr).line <= a.line, "a line past the re-wrapped block clamps to its last line");
  }

  // ---- follow mode, a still viewport while scrolled back, the marker ----
  {
    RolltuiDocument doc;
    doc.push_back(verbatim("n", lines(8)));
    doc.push_back(verbatim("s", "one"));  // verbatim: every "\n" is a line (markdown would join them)
    TranscriptHandle tr;
    layout(tr, doc, {0, 0, 20, 5}, opt);
    check(top_line(tr) == total_lines(tr) - 5 && lines_below(tr) == 0, "following: nothing below the viewport");
    Frame f(20, 5);
    draw(tr, f, theme, scratch);
    check(row_text(f, 4) == "one", "the last row is the last line while following");
    doc[1].text += "\n\ntwo";
    doc[1].version++;
    layout(tr, doc, {0, 0, 20, 5}, opt);
    f = Frame(20, 5);
    draw(tr, f, theme, scratch);
    check(row_text(f, 4) == "two", "new lines scroll a following view");
    scroll_by(tr, -1);
    check(!scroll(tr).follow, "an upward scroll clears follow");
    const std::size_t top = top_line(tr);
    for (int i = 0; i < 3; ++i) { doc[1].text += "\nmore"; doc[1].version++; }
    layout(tr, doc, {0, 0, 20, 5}, opt);
    check(top_line(tr) == top, "a growing document never moves a non-following view");
    check(lines_below(tr) == 4, "four lines are hidden below (1 scrolled + 3 new): " + std::to_string(lines_below(tr)));
    f = Frame(20, 5);
    draw(tr, f, theme, scratch);
    check(row_text(f, 4).find("\xE2\x96\xBC 4 more") != std::string::npos, "the marker names the hidden lines: [" + row_text(f, 4) + "]");
    scroll_to_bottom(tr);
    layout(tr, doc, {0, 0, 20, 5}, opt);
    f = Frame(20, 5);
    draw(tr, f, theme, scratch);
    check(scroll(tr).follow && row_text(f, 4).find("\xE2\x96\xBC") == std::string::npos, "End re-engages follow and the marker goes");
    scroll_page(tr, -1);
    check(top_line(tr) == total_lines(tr) - 5 - 4, "PageUp moves by the viewport height minus one");
    scroll_page(tr, 1);
    check(scroll(tr).follow, "PageDown back to the bottom re-engages follow");
  }

  // ---- folding ----
  {
    RolltuiDocument doc;
    doc.push_back(user("u", "go"));
    doc.push_back(tool("t", "read_file X", "a\nb\nc"));
    doc.push_back(md("a", "done"));
    TranscriptHandle tr;
    layout(tr, doc, {0, 0, 30, 8}, opt);
    const RolltuiEntryLayout* L = layout_of(tr, 1);
    check(L && L->folded && lines_of(*L).size() == 1 && L->hidden_lines == 3 && text_of(*L) == "read_file X",
          "a foldable entry starts folded: one summary line, three hidden, its text is the summary");
    Frame f(30, 8);
    draw(tr, f, theme, scratch);
    check(row_text(f, 2) == "\xE2\x96\xB8 read_file X (3 lines)", "the summary line: [" + row_text(f, 2) + "]");
    handle(tr, mouse(MouseEvent::Kind::Press, 5, 2), doc, 1000);
    handle(tr, mouse(MouseEvent::Kind::Release, 5, 2), doc, 1000);
    layout(tr, doc, {0, 0, 30, 8}, opt);
    L = layout_of(tr, 1);
    check(L && !L->folded && lines_of(*L).size() == 4 && text_of(*L) == "a\nb\nc", "a click on the summary line unfolds: summary + 3 body lines, text is the body");
    check(!selection(tr).active, "the click selected nothing");
    f = Frame(30, 8);
    draw(tr, f, theme, scratch);
    check(row_text(f, 2) == "\xE2\x96\xBE read_file X" && row_text(f, 3) == "a", "unfolded: ▾ summary then the body");
    check(handle(tr, ctrl('o'), doc, 2000), "Ctrl-O finds the first fold in view");
    layout(tr, doc, {0, 0, 30, 8}, opt);
    check(is_folded(tr, doc[1]) && lines_of(*layout_of(tr, 1)).size() == 1, "and toggles it back");
    scroll_to_top(tr);
    layout(tr, doc, {0, 0, 30, 1}, opt);  // a one-row viewport showing only the prompt
    check(!handle(tr, ctrl('o'), doc, 3000), "Ctrl-O with no summary line in view does nothing");
  }

  // ---- hit-testing and a drag inside one entry ----
  {
    RolltuiDocument doc;
    doc.push_back(user("u", "hello world"));
    TranscriptHandle tr;
    std::string copied;
    int copies = 0;
    CopySink copy_sink{&copied, &copies};
    rolltui_transcript_set_copy(tr, copy_trampoline, &copy_sink);
    layout(tr, doc, {0, 0, 20, 3}, opt);
    auto p = hit(tr, 2, 0);
    check(p && *p == RolltuiTextPos{0, 0, 1}, "the cell under 'h' is (entry 0, offset 0, 1 byte)");
    p = hit(tr, 0, 0);
    check(p && *p == RolltuiTextPos{0, 0, 0}, "a chrome cell (the prompt prefix) maps to the nearest text: the start of 'hello'");
    p = hit(tr, 19, 0);
    check(p && *p == RolltuiTextPos{0, 10, 1}, "past the end of the line: the last grapheme, inclusive");
    p = hit(tr, 3, 2);
    check(p && *p == RolltuiTextPos{0, 11, 0}, "a row below the text: the end of the last entry");
    handle(tr, mouse(MouseEvent::Kind::Press, 2, 0), doc, 1000);
    check(selection(tr).active && selected_text(tr) == "h", "a press selects the grapheme under the pointer");
    handle(tr, mouse(MouseEvent::Kind::Drag, 6, 0), doc, 1050);
    check(selected_text(tr) == "hello", "dragging right includes the cell under the pointer: 'hello'");
    handle(tr, mouse(MouseEvent::Kind::Drag, 0, 0), doc, 1100);
    check(selected_text(tr) == "h", "dragging back left onto chrome shrinks to the anchor grapheme");
    handle(tr, mouse(MouseEvent::Kind::Drag, 6, 0), doc, 1150);
    handle(tr, mouse(MouseEvent::Kind::Release, 6, 0), doc, 1200);
    check(copies == 1 && copied == "hello", "release copies on select (" + copied + ")");
    Frame f(20, 3);
    draw(tr, f, theme, scratch);
    const Style sel = theme.style(Role::selection);
    const bool selected_bg = sel.bg.kind != Color::Kind::None;
    check(selected_bg && f.at(2, 0).style.bg == sel.bg && f.at(6, 0).style.bg == sel.bg && f.at(7, 0).style.bg != sel.bg,
          "the selected cells wear the selection background; the next cell does not");
    check(f.at(0, 0).style.bg != sel.bg, "the prefix (chrome) is not highlighted when the line's text is only partly selected");
    check(handle(tr, alt('c'), doc, 1300) && copies == 2, "Alt-C copies again");
    check(handle(tr, key(ROLLTUI_KEY_ESCAPE), doc, 1400) && !selection(tr).active, "Escape clears the selection");
    check(!handle(tr, key(ROLLTUI_KEY_ESCAPE), doc, 1500), "and is not consumed when there is none");
    // A plain click (no drag) selects nothing.
    handle(tr, mouse(MouseEvent::Kind::Press, 4, 0), doc, 2000);
    handle(tr, mouse(MouseEvent::Kind::Release, 4, 0), doc, 2010);
    check(!selection(tr).active && copies == 2, "a click without a drag selects nothing and copies nothing");
    // Dragging leftwards from the anchor also includes the grapheme under the pointer.
    handle(tr, mouse(MouseEvent::Kind::Press, 6, 0), doc, 3000);
    handle(tr, mouse(MouseEvent::Kind::Drag, 2, 0), doc, 3050);
    check(selected_text(tr) == "hello", "dragging leftwards: the pointer's cell is included too");
    handle(tr, mouse(MouseEvent::Kind::Release, 2, 0), doc, 3100);
  }

  // ---- selection across a wrap and across entries: no wrap artefacts, bullets kept ----
  {
    RolltuiDocument doc;
    doc.push_back(verbatim("v", "alpha beta gamma delta"));
    doc.push_back(md("m", "- item one\n- item two"));
    TranscriptHandle tr;
    std::string copied;
    CopySink copy_sink{&copied};
    rolltui_transcript_set_copy(tr, copy_trampoline, &copy_sink);
    layout(tr, doc, {0, 0, 12, 5}, opt);
    Frame f(12, 5);
    draw(tr, f, theme, scratch);
    check(row_text(f, 0) == "alpha beta" && row_text(f, 1) == "gamma delta" && row_text(f, 3) == "\xE2\x80\xA2 item one",
          "the fixture wraps as expected: [" + row_text(f, 0) + "|" + row_text(f, 1) + "|" + row_text(f, 3) + "]");
    handle(tr, mouse(MouseEvent::Kind::Press, 6, 0), doc, 1000);
    handle(tr, mouse(MouseEvent::Kind::Drag, 9, 4), doc, 1100);
    handle(tr, mouse(MouseEvent::Kind::Release, 9, 4), doc, 1200);
    check(copied == "beta gamma delta\n\xE2\x80\xA2 item one\n\xE2\x80\xA2 item two",
          "the copy is logical text: no break where the wrap was, entries joined by one newline, bullets kept [" + copied + "]");
    auto gap = hit(tr, 3, 2);
    check(gap && *gap == RolltuiTextPos{0, 22, 0}, "a gap row maps to the end of the previous entry");
    // Double-click a word, triple-click the logical line.
    handle(tr, mouse(MouseEvent::Kind::Press, 2, 1), doc, 5000);
    handle(tr, mouse(MouseEvent::Kind::Release, 2, 1), doc, 5010);
    handle(tr, mouse(MouseEvent::Kind::Press, 2, 1), doc, 5100);
    check(selected_text(tr) == "gamma", "a double-click selects the UAX #29 word under the pointer: [" + selected_text(tr) + "]");
    handle(tr, mouse(MouseEvent::Kind::Release, 2, 1), doc, 5110);
    check(copied == "gamma", "and the release copies it");
    handle(tr, mouse(MouseEvent::Kind::Press, 2, 1), doc, 5200);
    check(selected_text(tr) == "alpha beta gamma delta", "a third click selects the logical line — the whole unwrapped paragraph");
    handle(tr, mouse(MouseEvent::Kind::Release, 2, 1), doc, 5210);
    handle(tr, mouse(MouseEvent::Kind::Press, 2, 1), doc, 9000);  // too late to pair
    check(selected_text(tr) == "m", "a press after the pairing window starts over (one grapheme, the 'm' under x=2)");
    handle(tr, mouse(MouseEvent::Kind::Release, 2, 1), doc, 9010);
    // A double-click followed by a drag grows the selection by whole words.
    handle(tr, mouse(MouseEvent::Kind::Press, 2, 1), doc, 12000);
    handle(tr, mouse(MouseEvent::Kind::Release, 2, 1), doc, 12010);
    handle(tr, mouse(MouseEvent::Kind::Press, 2, 1), doc, 12100);
    handle(tr, mouse(MouseEvent::Kind::Drag, 7, 1), doc, 12200);  // into "delta"
    check(selected_text(tr) == "gamma delta", "dragging after a double-click extends by whole words: [" + selected_text(tr) + "]");
    handle(tr, mouse(MouseEvent::Kind::Drag, 7, 0), doc, 12300);  // back up into "beta"
    check(selected_text(tr) == "beta gamma", "and backwards from the word's end: [" + selected_text(tr) + "]");
    handle(tr, mouse(MouseEvent::Kind::Release, 7, 0), doc, 12400);
    check(copied == "beta gamma", "release copies the word-extended selection");
    handle(tr, mouse(MouseEvent::Kind::Release, 2, 1), doc, 9010);
    // Shift+press extends.
    handle(tr, mouse(MouseEvent::Kind::Press, 0, 0), doc, 20000);
    handle(tr, mouse(MouseEvent::Kind::Drag, 4, 0), doc, 20050);
    handle(tr, mouse(MouseEvent::Kind::Release, 4, 0), doc, 20100);
    handle(tr, mouse(MouseEvent::Kind::Press, 3, 1, true), doc, 21000);
    check(selected_text(tr) == "alpha beta gamm", "Shift+press extends the selection to the pointer: [" + selected_text(tr) + "]");
    handle(tr, mouse(MouseEvent::Kind::Release, 3, 1, true), doc, 21100);
  }

  // ---- edge auto-scroll while dragging ----
  {
    RolltuiDocument doc;
    doc.push_back(verbatim("n", lines(30)));
    TranscriptHandle tr;
    std::string copied;
    CopySink copy_sink{&copied};
    rolltui_transcript_set_copy(tr, copy_trampoline, &copy_sink);
    layout(tr, doc, {0, 2, 20, 5}, opt);  // the area starts at row 2, so edges are not row 0
    scroll_to_top(tr);
    handle(tr, mouse(MouseEvent::Kind::Press, 0, 2), doc, 1000);  // "line 1"
    check(!wants_tick(tr), "no ticks wanted while the pointer is inside");
    handle(tr, mouse(MouseEvent::Kind::Drag, 0, 9), doc, 1050);  // three rows past the bottom edge (row 6)
    check(wants_tick(tr), "a drag past the bottom edge asks for ticks");
    check(selected_text(tr) == "line 1\nline 2\nline 3\nline 4\nl", "meanwhile the head sits on the last visible row");
    tick(tr);
    check(top_line(tr) == 3, "one tick scrolls by the distance past the edge (3 rows): top " + std::to_string(top_line(tr)));
    check(selected_text(tr) == "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nl", "and the head follows to the new last row");
    tick(tr);
    check(top_line(tr) == 6, "ticks keep scrolling while the pointer stays out");
    handle(tr, mouse(MouseEvent::Kind::Drag, 0, 4), doc, 1300);
    check(!wants_tick(tr), "back inside: no more ticks");
    handle(tr, mouse(MouseEvent::Kind::Drag, 0, 0), doc, 1350);  // two rows above the top edge
    check(wants_tick(tr), "past the top edge asks for ticks too");
    tick(tr);
    check(top_line(tr) == 4, "and scrolls up by the distance (2): top " + std::to_string(top_line(tr)));
    handle(tr, mouse(MouseEvent::Kind::Release, 0, 0), doc, 1400);
    check(!copied.empty() && copied.find("line 1") == 0, "release after an auto-scrolled drag copies the span");
    check(!scroll(tr).follow, "auto-scrolling up leaves follow off");
    // Dragging to the very bottom re-engages follow.
    handle(tr, mouse(MouseEvent::Kind::Press, 0, 2), doc, 3000);
    handle(tr, mouse(MouseEvent::Kind::Drag, 0, 60), doc, 3050);
    for (int i = 0; i < 40 && wants_tick(tr); ++i) tick(tr);
    check(scroll(tr).follow && top_line(tr) == total_lines(tr) - 5, "a drag held past the bottom reaches the end and follow re-engages");
    handle(tr, mouse(MouseEvent::Kind::Release, 0, 60), doc, 3100);
  }

  // ---- hyperlinks: OSC 8 from the parsed URL; chrome never copies ----
  {
    RolltuiDocument doc;
    doc.push_back(md("m", "see [docs](https://example.com/d) now\n\n```sh\nmake\n```"));
    TranscriptHandle tr;
    layout(tr, doc, {0, 0, 40, 6}, opt);
    Frame f(40, 6);
    draw(tr, f, theme, scratch);
    int linked = 0;
    std::string url;
    for (int x = 0; x < 40; ++x)
      if (f.at(x, 0).link != 0) { ++linked; url = std::string(f.link(f.at(x, 0).link)); }
    check(linked == 4 && url == "https://example.com/d", "exactly the link text's 4 cells carry the parsed URL (" + std::to_string(linked) + ", " + url + ")");
    std::string bytes = render_full(f, ROLLTUI_DEPTH_TRUECOLOR);
    const std::size_t open = bytes.find("\x1b]8;;https://example.com/d\x1b\\");
    const std::size_t glyphs = bytes.find("docs\x1b]8;;\x1b\\");
    check(open != std::string::npos && glyphs != std::string::npos && open < glyphs && bytes.find("docs") == glyphs,
          "the diff opens the link before its first glyph (the link's SGR may follow the open) and closes it right after the last");
    Frame plain(40, 6);
    plain.put_text(0, 0, "no links here", theme.style(Role::text), 40);
    check(render_full(plain, ROLLTUI_DEPTH_TRUECOLOR).find("]8;;") == std::string::npos, "a frame without links emits no OSC 8");
    // A changed link is a changed cell.
    Frame g = f;
    g.put_text(4, 0, "docs", theme.style(Role::md_link), 4, false, g.link_id("https://other/"));
    check(render_diff(&f, g, ROLLTUI_DEPTH_TRUECOLOR).find("https://other/") != std::string::npos, "a link change alone redraws the cell");
    // Select everything: the code box's rules and bars never reach the copy.
    handle(tr, mouse(MouseEvent::Kind::Press, 0, 0), doc, 1000);
    handle(tr, mouse(MouseEvent::Kind::Drag, 39, 5), doc, 1100);
    std::string all = selected_text(tr);
    handle(tr, mouse(MouseEvent::Kind::Release, 39, 5), doc, 1200);
    check(all.find("see docs (https://example.com/d) now") == 0 && all.find("make") != std::string::npos,
          "the whole entry's logical text: link text with its URL, then the code line [" + all + "]");
    check(all.find("\xE2\x94\x80") == std::string::npos && all.find("\xE2\x94\x82") == std::string::npos,
          "no box-drawing chrome in the copy");
    f = Frame(40, 6);
    draw(tr, f, theme, scratch);
    const Style sel = theme.style(Role::selection);
    check(f.at(0, 3).style.bg == sel.bg, "the code box's border highlights with a wholly selected line");
  }

  // ---- degenerate inputs ----
  {
    RolltuiDocument empty;
    TranscriptHandle tr;
    layout(tr, empty, {0, 0, 10, 3}, opt);
    Frame f(10, 3);
    draw(tr, f, theme, scratch);
    check(total_lines(tr) == 0 && !hit(tr, 0, 0) && !handle(tr, mouse(MouseEvent::Kind::Press, 0, 0), empty, 0) == false,
          "an empty document: no lines, no hit, a press is harmless");
    check(!selection(tr).active, "and no selection");
    RolltuiDocument doc;
    doc.push_back(md("m", "x"));
    layout(tr, doc, {0, 0, 0, 0}, opt);
    draw(tr, f, theme, scratch);
    scroll_by(tr, 5);
    scroll_page(tr, -1);
    check(total_lines(tr) == 1 && top_line(tr) == 0, "a zero-size area lays out without dividing by anything");
    RolltuiTranscriptOptions inset = opt;
    inset.inset = 1;
    layout(tr, doc, {0, 0, 2, 1}, inset);
    check(text_area(tr).w == 2, "an inset that would leave no column is not applied");
    layout(tr, doc, {0, 0, 3, 1}, inset);
    check(text_area(tr).x == 1 && text_area(tr).w == 1, "an inset of 1 on a 3-wide area leaves one column");
  }

  // ---- the "▼ N more" marker -------------------------------------------
  // KEPT alongside the scrollbar, not replaced by it : the marker
  // is the NON-GRAPHICAL signal and the bar is the positional one. What the bar buys is
  // permission for the marker to get cheaper when narrow.
  {
    check(scroll_marker_text(0, 40, false).empty(), "nothing below → no marker at all");
    check(scroll_marker_text(57, 78, false) == "\xE2\x96\xBC 57 more ", "with room, the full form");
    check(scroll_marker_text(198, 18, false) == "\xE2\x96\xBC" "198",
          "at 18 cells the full form would eat the row, so the count alone [" + scroll_marker_text(198, 18, false) + "]");
    check(scroll_marker_text(198, 3, false) == "\xE2\x96\xBC", "and at 3 cells the arrow alone still says there is more");
    check(scroll_marker_text(198, 0, false).empty(), "at 0 cells, nothing (the standing degenerate-size rule)");
    // Never wider than it was given — the property that stops it writing outside the area.
    bool fits = true;
    for (std::size_t below = 1; below < 5000; below += 7)
      for (int w = 1; w <= 40; ++w)
        if (unicode::display_width(scroll_marker_text(below, w, false), false) > w) fits = false;
    check(fits, "over every (below, width): the marker never exceeds the width it was given");

    // A click on it scrolls to the bottom and re-engages follow, rather than starting a
    // drag-SELECT: a control-shaped thing does the control's job.
    RolltuiDocument doc;
    for (int i = 0; i < 40; ++i) doc.push_back(verbatim(("m" + std::to_string(i)).c_str(), "line " + std::to_string(i)));
    TranscriptHandle tr;
    layout(tr, doc, {0, 0, 40, 6}, opt);
    scroll_to_top(tr);
    layout(tr, doc, {0, 0, 40, 6}, opt);
    check(top_line(tr) == 0 && !scroll(tr).follow, "scrolled to the top, not following");
    const std::string marker = scroll_marker_text(lines_below(tr), 40, false);
    check(!marker.empty(), "…and the marker is showing (" + marker + ")");
    const int mw = static_cast<int>(unicode::display_width(marker, false));
    handle(tr, mouse(MouseEvent::Kind::Press, 40 - mw, 5), doc, 1000);
    layout(tr, doc, {0, 0, 40, 6}, opt);
    check(scroll(tr).follow && lines_below(tr) == 0, "a click on the marker scrolls to the bottom and re-engages follow");
    check(!selection(tr).active, "…and selects nothing — it is a control, not text");
  }

  // ---- FIND ------------------------------------------------------------
  // Matches live in LOGICAL text, so every assertion below is on the model except the
  // two that are about what a cell got painted — which is the only place the wrap
  // behaviour can actually be read.
  {
    const Color match_bg = theme.style(Role::find_match).bg;
    const Color current_bg = theme.style(Role::find_current).bg;
    // The rows a find highlight touched, and which kind. Reading the frame rather than
    // the model is the point here: "highlights on both rows" is a claim about cells.
    auto find_rows = [&](const Frame& f, Color bg) {
      std::vector<int> rows;
      for (int y = 0; y < f.height(); ++y)
        for (int x = 0; x < f.width(); ++x)
          if (f.at(x, y).style.bg == bg) { rows.push_back(y); break; }
      return rows;
    };

    RolltuiDocument doc;
    doc.push_back(user("u1", "find the needle"));
    doc.push_back(md("a1", "A needle in a paragraph, and another needle after it."));
    doc.push_back(tool("t1", "read_file haystack.txt", "one\nthe needle is in here\nthree"));
    doc.push_back(verbatim("n1", "no match on this line"));

    TranscriptHandle tr;
    layout(tr, doc, {0, 0, 40, 8}, opt);
    check(match_count(tr) == 0 && current_match_number(tr) == 0 && query(tr).empty(),
          "no query: no matches, no current, nothing to render");

    // (a) A FOLDED entry is searched UNFOLDED, so the count does not depend on which
    // blocks happen to be open — the whole reason searchable_text exists.
    check(is_folded(tr, doc[2]), "the tool entry starts folded");
    set_query(tr, "needle");
    layout(tr, doc, {0, 0, 40, 8}, opt);
    check(match_count(tr) == 4,
          "four matches: one in the prompt, two in the answer, and ONE INSIDE THE FOLDED BLOCK (" +
              std::to_string(match_count(tr)) + ")");
    const std::size_t folded_hits = [&] {
      std::size_t n = 0;
      // the matches are counted and indexed — nothing on the C side can hand
      // back a `std::vector<FindMatch>` without building one per call.
      for (std::size_t k = 0; k < match_count(tr); ++k) if (match_at(tr, k).entry == 2) ++n;
      return n;
    }();
    check(folded_hits == 1, "…and it is attributed to the folded entry, not to its summary");

    // (b) Revealing a match inside a folded block UNFOLDS it.
    // BOUNDED by the match count on purpose. Written unbounded first, it HUNG under this
    // milestone's own negative control (which removes the folded-block match, so the
    // cycle never reaches entry 2) — and a test that hangs cannot tell you what it found,
    // exactly as CLAUDE.md says of one that crashes. The bound makes the control fail
    // with a named assertion instead.
    for (std::size_t step = 0; step <= match_count(tr) && current_match(tr) && current_match(tr)->entry != 2; ++step) {
      find_next(tr);
      layout(tr, doc, {0, 0, 40, 8}, opt);
    }
    check(current_match(tr) && current_match(tr)->entry == 2, "stepped to the match inside the folded block");
    check(!is_folded(tr, doc[2]), "…and revealing it UNFOLDED the block");
    {
      Frame f(40, 8);
      draw(tr, f, theme, scratch);
      check(!find_rows(f, current_bg).empty(), "…the current match is on screen and painted in find_current");
    }

    // (c) The count and the position are readable, and stepping wraps.
    set_query(tr, "needle");  // same query: a no-op, current match kept
    const std::size_t at = current_match_number(tr);
    check(at >= 1 && at <= 4, "the current match has a 1-based position for a host to print (" + std::to_string(at) + "/4)");
    for (std::size_t i = 0; i < 4; ++i) { find_next(tr); layout(tr, doc, {0, 0, 40, 8}, opt); }
    check(current_match_number(tr) == at, "four find_next on four matches wraps exactly back to where it started");
    find_prev(tr);
    layout(tr, doc, {0, 0, 40, 8}, opt);
    check(current_match_number(tr) == (at == 1 ? 4 : at - 1), "find_prev steps back and wraps the other way");

    // (c2) The OTHER matches paint too, in find_match and not in find_current — the two
    // roles are what make "which of the four am I on" answerable, so both must be on
    // screen at once for the claim to mean anything.
    {
      RolltuiDocument two;
      two.push_back(verbatim("p", "needle one\nneedle two"));
      TranscriptHandle trm;
      layout(trm, two, {0, 0, 20, 4}, opt);
      set_query(trm, "needle");
      layout(trm, two, {0, 0, 20, 4}, opt);
      Frame f(20, 4);
      draw(trm, f, theme, scratch);
      const std::vector<int> cur = find_rows(f, current_bg);
      const std::vector<int> oth = find_rows(f, match_bg);
      check(match_count(trm) == 2 && cur.size() == 1 && oth.size() == 1 && cur[0] != oth[0],
            "with two matches in view, exactly one row carries find_current and the other find_match (" +
                std::to_string(cur.size()) + "/" + std::to_string(oth.size()) + ")");
    }

    // (d) ASCII-case-insensitive, and non-overlapping.
    set_query(tr, "NEEDLE");
    layout(tr, doc, {0, 0, 40, 8}, opt);
    check(match_count(tr) == 4, "the search is ASCII-case-insensitive (Transcript.hpp states it rather than inferring it)");
    RolltuiDocument aaa;
    aaa.push_back(verbatim("r", "aaaa"));
    TranscriptHandle tr2;
    layout(tr2, aaa, {0, 0, 20, 4}, opt);
    set_query(tr2, "aa");
    layout(tr2, aaa, {0, 0, 20, 4}, opt);
    check(match_count(tr2) == 2, "matches do not overlap: 'aa' in 'aaaa' is two, not three");

    // (e) A match that WRAPS across two rows highlights on both — the property that
    // comes free from matching logical text and testing each cell's source offset.
    RolltuiDocument wrapped;
    wrapped.push_back(verbatim("w", "zz aaaaaaaaaaaaaaaa zz"));
    TranscriptHandle tr3;
    layout(tr3, wrapped, {0, 0, 8, 6}, opt);
    set_query(tr3, "aaaaaaaaaaaa");  // 12 a's: cannot fit on one 8-cell row
    layout(tr3, wrapped, {0, 0, 8, 6}, opt);
    check(match_count(tr3) == 1, "one match, longer than the row is wide");
    {
      Frame f(8, 6);
      draw(tr3, f, theme, scratch);
      const std::vector<int> rows = find_rows(f, current_bg);
      check(rows.size() >= 2, "…and it is highlighted on BOTH rows it wrapped onto (" + std::to_string(rows.size()) + ")");
    }

    // (f) An empty query clears without moving the view.
    RolltuiDocument many;
    for (int i = 0; i < 12; ++i) many.push_back(verbatim(("e" + std::to_string(i)).c_str(), "needle " + std::to_string(i)));
    TranscriptHandle tr4;
    layout(tr4, many, {0, 0, 20, 4}, opt);
    scroll_to_top(tr4);
    scroll_by(tr4, 6);
    layout(tr4, many, {0, 0, 20, 4}, opt);
    const std::size_t before = top_line(tr4);
    set_query(tr4, "");
    layout(tr4, many, {0, 0, 20, 4}, opt);
    check(top_line(tr4) == before && match_count(tr4) == 0 && current_match_number(tr4) == 0,
          "an empty query clears the matches and does NOT move the view (" + std::to_string(top_line(tr4)) + " vs " +
              std::to_string(before) + ")");
    // …while a real query does move it, so the check above is about EMPTY and not about
    // find never scrolling (the negative control for it).
    set_query(tr4, "needle 11");
    layout(tr4, many, {0, 0, 20, 4}, opt);
    check(top_line(tr4) != before && match_count(tr4) == 1, "a query with a match further down does scroll to it");

    // (g) The selection wins where they overlap (Transcript.hpp's stated precedence).
    RolltuiDocument one;
    one.push_back(verbatim("s", "needle"));
    TranscriptHandle tr5;
    layout(tr5, one, {0, 0, 20, 3}, opt);
    set_query(tr5, "needle");
    layout(tr5, one, {0, 0, 20, 3}, opt);
    {
      Frame f(20, 3);
      draw(tr5, f, theme, scratch);
      check(!find_rows(f, current_bg).empty(), "the match paints before anything is selected");
    }
    select(tr5, {0, 0, 1}, {0, 5, 1});
    {
      Frame f(20, 3);
      draw(tr5, f, theme, scratch);
      check(f.at(0, 0).style.bg == theme.style(Role::selection).bg,
            "…and a selection over it wins: the user's most recent direct act is what the cell says");
    }

    // (h) The standing degenerate-size rule, extended to find.
    TranscriptHandle tr6;
    set_query(tr6, "needle");
    layout(tr6, one, {0, 0, 0, 0}, opt);
    Frame f0(1, 1);
    draw(tr6, f0, theme, scratch);
    find_next(tr6);
    layout(tr6, one, {0, 0, 1, 1}, opt);
    draw(tr6, f0, theme, scratch);
    check(match_count(tr6) == 1, "a 0- and a 1-cell area still find, still draw, still step");
  }

  // ---- long code blocks: folded, capped, clicked, and searched -------
  // The mechanism is the RENDERER's (Markdown.hpp); what is asserted here is the part
  // this widget owns — the toggles kept by id, the two click targets, and the property
  // the whole design was chosen for: the find count does not move when a block folds.
  {
    std::string block = "```diff\n";
    for (int i = 1; i <= 12; ++i) block += (i % 2 ? "-old " : "+new ") + std::string("needle") + std::to_string(i) + "\n";
    block += "```\n";
    RolltuiDocument doc;
    doc.push_back(md("c1", "before the block\n\n" + block + "\nafter the block\n"));
    RolltuiTranscriptOptions fold = opt;
    fold.code_fold_over_lines = 4;
    fold.code_cap_lines = 6;

    TranscriptHandle tr;
    layout(tr, doc, {0, 0, 40, 24}, fold);
    const RolltuiEntryLayout* L = layout_of(tr, 0);
    check(L && code_blocks_of(*L).size() == 1 && code_blocks_of(*L)[0].folded,
          "a 12-line block over the threshold arrives folded in the transcript");
    const std::size_t folded_total = total_lines(tr);
    const std::size_t header = code_blocks_of(*L)[0].header_line;
    check(header != ROLLTUI_MD_NO_LINE, "…and reports the row a click has to land on");
    Frame f(40, 24);
    draw(tr, f, theme, scratch);
    check(row_text(f, static_cast<int>(header)).find("\xE2\x96\xB8 diff \xC2\xB7 12 lines") != std::string::npos,
          "…which draws the summary, naming the language and the line count [" + row_text(f, static_cast<int>(header)) + "]");

    // Click 1: the header row toggles the fold, over its WHOLE row (x is anywhere).
    handle(tr, mouse(MouseEvent::Kind::Press, 30, static_cast<int>(header)), doc, 1000);
    layout(tr, doc, {0, 0, 40, 24}, fold);
    const RolltuiEntryLayout* open = layout_of(tr, 0);
    check(!code_blocks_of(*open)[0].folded && total_lines(tr) > folded_total,
          "a click anywhere on the header row unfolds it");
    check(code_blocks_of(*open)[0].hidden == 6 && code_blocks_of(*open)[0].marker_line != ROLLTUI_MD_NO_LINE,
          "…and the opened block is still CAPPED, with 6 of its 12 lines behind the marker");
    check(selection(tr).empty(), "…and it selected nothing: a control does its own job, not a drag");

    // Click 2: the "▼ N more" row lifts the cap for THAT block — the transcript's own marker
    // rule, one rung down.
    const std::size_t marker = code_blocks_of(*open)[0].marker_line;
    handle(tr, mouse(MouseEvent::Kind::Press, 5, static_cast<int>(marker)), doc, 2000);
    layout(tr, doc, {0, 0, 40, 24}, fold);
    check(code_blocks_of(*layout_of(tr, 0))[0].hidden == 0, "a click on the block's ▼ marker shows the rest of it");

    // Ctrl-O takes the nearest fold from the top, whether it is an entry's or a block's.
    set_code_folded(tr, "c1", 0, true);
    layout(tr, doc, {0, 0, 40, 24}, fold);
    check(code_blocks_of(*layout_of(tr, 0))[0].folded, "set_code_folded shuts it again");
    handle(tr, ctrl('o'), doc, 3000);
    layout(tr, doc, {0, 0, 40, 24}, fold);
    check(!code_blocks_of(*layout_of(tr, 0))[0].folded, "transcript.fold (Ctrl-O) toggles a code block too — no new action for it");

    // THE PROPERTY THE DESIGN EXISTS FOR. A fold hides lines and never text, so the
    // match count is the same open and shut. If this ever fails, a folded block has
    // started shifting the offsets after it and a highlight is landing on wrong bytes.
    set_query(tr, "needle");
    layout(tr, doc, {0, 0, 40, 24}, fold);
    const std::size_t open_matches = match_count(tr);
    check(open_matches == 12, "12 matches with the block open (" + std::to_string(open_matches) + ")");
    set_code_folded(tr, "c1", 0, true);
    layout(tr, doc, {0, 0, 40, 24}, fold);
    check(match_count(tr) == open_matches, "…and exactly the same count with it FOLDED, which is the whole rule");
    // …and revealing one of them has to OPEN the block, because the text was there but
    // the line was not. The query is set while the block is shut, so the reveal is the
    // only thing that could have opened it.
    check(code_blocks_of(*layout_of(tr, 0))[0].folded, "the block is shut when the query is typed");
    set_query(tr, "needle7");
    layout(tr, doc, {0, 0, 40, 24}, fold);
    check(match_count(tr) == 1 && !code_blocks_of(*layout_of(tr, 0))[0].folded,
          "revealing a match inside a folded block unfolds it");

    // A selection over the entry copies the block's real code, not its summary: the
    // logical text never lost it.
    set_query(tr, "");
    set_code_folded(tr, "c1", 0, true);
    set_code_uncapped(tr, "c1", 0, false);
    layout(tr, doc, {0, 0, 40, 24}, fold);
    const std::string_view text = text_of(*layout_of(tr, 0));
    select(tr, {0, 0, 0}, {0, text.size(), 0});
    check(selected_text(tr).find("needle7") != std::string::npos,
          "a selection over a folded block copies the CODE, because the fold never touched the text");
  }

  rolltui_draw_scratch_free(scratch);
  return report("rolltui transcript_test");
}
