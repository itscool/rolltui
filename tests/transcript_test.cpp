//
// transcript_test.cpp — the transcript widget's rules (Transcript.hpp): the layout
// cache re-lays only what changed; the scroll anchor survives a re-wrap; follow mode
// and the "▼ N more" marker; folding by click and Ctrl-O; hit-testing, drag selection
// in logical coordinates (no wrap artefacts in the copy, spanning entries, word and
// line clicks), edge auto-scroll; OSC 8 hyperlinks from parsed URLs; chrome never
// copies. No terminal: every check is on a Frame or on the widget's state.
//
#include <string>
#include <vector>

#include "rolltui/Document.hpp"
#include "rolltui/Screen.hpp"
#include "rolltui/Theme.hpp"
#include "rolltui/Transcript.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui_test;

namespace {

DocEntry user(const char* id, std::string text) {
  DocEntry e;
  e.id = id;
  e.text = std::move(text);
  e.markdown = false;
  e.prefix = "> ";
  e.prefix_role = Role::prompt;
  return e;
}
DocEntry md(const char* id, std::string text) {
  DocEntry e;
  e.id = id;
  e.text = std::move(text);
  e.markdown = true;
  return e;
}
DocEntry verbatim(const char* id, std::string text) {
  DocEntry e;
  e.id = id;
  e.text = std::move(text);
  e.markdown = false;
  return e;
}
DocEntry tool(const char* id, std::string summary, std::string text) {
  DocEntry e = verbatim(id, std::move(text));
  e.foldable = true;
  e.summary = std::move(summary);
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
KeyEvent key(Key k) { KeyEvent e; e.key = k; return e; }
KeyEvent ctrl(char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); e.ctrl = true; return e; }
KeyEvent alt(char c) { KeyEvent e; e.key = Key::Char; e.ch = static_cast<char32_t>(c); e.alt = true; return e; }

std::string row_text(const Frame& f, int y) {
  std::string s;
  for (int x = 0; x < f.width(); ++x)
    if (!f.at(x, y).continuation) s += f.at(x, y).text;
  std::size_t end = s.find_last_not_of(' ');
  return end == std::string::npos ? "" : s.substr(0, end + 1);
}

std::string lines(int n) {
  std::string s;
  for (int i = 1; i <= n; ++i) s += "line " + std::to_string(i) + (i < n ? "\n" : "");
  return s;
}

}  // namespace

int main() {
  const Theme& theme = *builtin_theme("default-dark");
  TranscriptOptions opt;
  opt.gap = 1;

  // ---- the layout cache: only what changed is re-laid ----
  {
    Document doc;
    doc.entries.push_back(user("u1", "hello world"));
    doc.entries.push_back(md("a1", "# Plan\n\nA paragraph that is long enough to wrap at forty cells, with a [link](https://example.com/x) in it.\n\n```sh\ncmake --build build\n```\n\n- one\n- two"));
    doc.entries.push_back(verbatim("n1", "[roll] a note"));
    doc.entries.push_back(tool("t1", "read_file README.md", "line one\nline two\nline three"));
    doc.entries.push_back(md("a2", "streaming"));
    Transcript tr;
    tr.layout(doc, {0, 0, 40, 10}, opt);
    check(tr.stats().entries_relaid == 5, "first layout lays out every entry (" + std::to_string(tr.stats().entries_relaid) + ")");
    tr.layout(doc, {0, 0, 40, 10}, opt);
    check(tr.stats().entries_relaid == 0, "an identical frame re-lays nothing");
    doc.entries[4].text += " more text";
    doc.entries[4].version++;
    tr.layout(doc, {0, 0, 40, 10}, opt);
    check(tr.stats().entries_relaid == 1, "a streaming chunk (version bump) re-lays exactly the streaming entry");
    doc.entries[4].text += " again";  // no version bump: the host's bug, and the rule
    tr.layout(doc, {0, 0, 40, 10}, opt);
    check(tr.stats().entries_relaid == 0, "without a version bump the cache is trusted (the key, never the event)");
    doc.entries[4].version++;
    tr.layout(doc, {0, 0, 60, 10}, opt);
    check(tr.stats().entries_relaid == 5, "a new width re-lays everything once");
    tr.layout(doc, {0, 0, 60, 10}, opt);
    check(tr.stats().entries_relaid == 0, "and then nothing");
    tr.toggle_fold(doc.entries[3]);
    tr.layout(doc, {0, 0, 60, 10}, opt);
    check(tr.stats().entries_relaid == 1, "toggling a fold re-lays that entry only");
    check(tr.stats().cache_size == 5, "the cache holds one layout per entry (" + std::to_string(tr.stats().cache_size) + ")");
    // Sweep: a document whose ids all change leaves the old ids behind until the
    // cache outgrows it.
    Document other;
    for (int i = 0; i < 100; ++i) other.entries.push_back(verbatim(("x" + std::to_string(i)).c_str(), "x"));
    tr.layout(other, {0, 0, 60, 10}, opt);
    Document tiny;
    tiny.entries.push_back(verbatim("only", "y"));
    tr.layout(tiny, {0, 0, 60, 10}, opt);
    check(tr.stats().cache_size <= 1 + 32 + 2, "stale ids are swept when the cache outgrows the document (" + std::to_string(tr.stats().cache_size) + ")");
  }

  // ---- the anchor survives a re-wrap ----
  {
    Document doc;
    doc.entries.push_back(user("u", "first"));
    doc.entries.push_back(md("a", "Alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi omega, and once more: alpha beta gamma delta epsilon zeta eta theta iota kappa lambda."));
    doc.entries.push_back(verbatim("n", lines(12)));
    Transcript tr;
    tr.layout(doc, {0, 0, 30, 6}, opt);
    check(tr.scroll().follow && tr.top_line() == tr.total_lines() - 6, "a fresh transcript follows the bottom");
    tr.scroll_to_top();
    tr.scroll_by(3);  // into entry 1: its block is 1 gap line + wrapped lines, so line 2 = its second content line
    check(tr.scroll().entry == 1 && tr.scroll().line == 2 && !tr.scroll().follow,
          "the anchor is (entry, line within block): entry 1 line 2 after Home + 3");
    Frame f30(30, 6);
    tr.draw(f30, theme);
    const std::string top30 = row_text(f30, 0);
    const std::size_t total30 = tr.total_lines();
    tr.layout(doc, {0, 0, 60, 6}, opt);
    check(tr.total_lines() < total30, "a wider layout has fewer lines");
    check(tr.scroll().entry == 1 && tr.scroll().line == 2, "the same entry and line stay at the top after the re-wrap");
    Frame f(60, 6);
    tr.draw(f, theme);
    auto top_hit = tr.hit(0, 0);
    check(top_hit && top_hit->entry == 1 && row_text(f, 0) != top30 && row_text(f, 0).find("Alpha") == std::string::npos,
          "the top row is entry 1's second content line at the new width (re-wrapped, not the first): [" + row_text(f, 0) + "]");
    // A line index beyond the entry's new (shorter) block clamps rather than jumping.
    tr.layout(doc, {0, 0, 30, 6}, opt);
    tr.scroll_to_top();
    tr.scroll_by(8);
    const ScrollAnchor a = tr.scroll();
    tr.layout(doc, {0, 0, 200, 6}, opt);
    check(tr.scroll().entry == a.entry && tr.scroll().line <= a.line, "a line past the re-wrapped block clamps to its last line");
  }

  // ---- follow mode, a still viewport while scrolled back, the marker ----
  {
    Document doc;
    doc.entries.push_back(verbatim("n", lines(8)));
    doc.entries.push_back(verbatim("s", "one"));  // verbatim: every "\n" is a line (markdown would join them)
    Transcript tr;
    tr.layout(doc, {0, 0, 20, 5}, opt);
    check(tr.top_line() == tr.total_lines() - 5 && tr.lines_below() == 0, "following: nothing below the viewport");
    Frame f(20, 5);
    tr.draw(f, theme);
    check(row_text(f, 4) == "one", "the last row is the last line while following");
    doc.entries[1].text += "\n\ntwo";
    doc.entries[1].version++;
    tr.layout(doc, {0, 0, 20, 5}, opt);
    f = Frame(20, 5);
    tr.draw(f, theme);
    check(row_text(f, 4) == "two", "new lines scroll a following view");
    tr.scroll_by(-1);
    check(!tr.scroll().follow, "an upward scroll clears follow");
    const std::size_t top = tr.top_line();
    for (int i = 0; i < 3; ++i) { doc.entries[1].text += "\nmore"; doc.entries[1].version++; }
    tr.layout(doc, {0, 0, 20, 5}, opt);
    check(tr.top_line() == top, "a growing document never moves a non-following view");
    check(tr.lines_below() == 4, "four lines are hidden below (1 scrolled + 3 new): " + std::to_string(tr.lines_below()));
    f = Frame(20, 5);
    tr.draw(f, theme);
    check(row_text(f, 4).find("\xE2\x96\xBC 4 more") != std::string::npos, "the marker names the hidden lines: [" + row_text(f, 4) + "]");
    tr.scroll_to_bottom();
    tr.layout(doc, {0, 0, 20, 5}, opt);
    f = Frame(20, 5);
    tr.draw(f, theme);
    check(tr.scroll().follow && row_text(f, 4).find("\xE2\x96\xBC") == std::string::npos, "End re-engages follow and the marker goes");
    tr.scroll_page(-1);
    check(tr.top_line() == tr.total_lines() - 5 - 4, "PageUp moves by the viewport height minus one");
    tr.scroll_page(1);
    check(tr.scroll().follow, "PageDown back to the bottom re-engages follow");
  }

  // ---- folding ----
  {
    Document doc;
    doc.entries.push_back(user("u", "go"));
    doc.entries.push_back(tool("t", "read_file X", "a\nb\nc"));
    doc.entries.push_back(md("a", "done"));
    Transcript tr;
    tr.layout(doc, {0, 0, 30, 8}, opt);
    const EntryLayout* L = tr.layout_of(1);
    check(L && L->folded && L->lines.size() == 1 && L->hidden_lines == 3 && L->text == "read_file X",
          "a foldable entry starts folded: one summary line, three hidden, its text is the summary");
    Frame f(30, 8);
    tr.draw(f, theme);
    check(row_text(f, 2) == "\xE2\x96\xB8 read_file X (3 lines)", "the summary line: [" + row_text(f, 2) + "]");
    tr.handle(mouse(MouseEvent::Kind::Press, 5, 2), doc, 1000);
    tr.handle(mouse(MouseEvent::Kind::Release, 5, 2), doc, 1000);
    tr.layout(doc, {0, 0, 30, 8}, opt);
    L = tr.layout_of(1);
    check(L && !L->folded && L->lines.size() == 4 && L->text == "a\nb\nc", "a click on the summary line unfolds: summary + 3 body lines, text is the body");
    check(!tr.selection().active, "the click selected nothing");
    f = Frame(30, 8);
    tr.draw(f, theme);
    check(row_text(f, 2) == "\xE2\x96\xBE read_file X" && row_text(f, 3) == "a", "unfolded: ▾ summary then the body");
    check(tr.handle(ctrl('o'), doc, 2000), "Ctrl-O finds the first fold in view");
    tr.layout(doc, {0, 0, 30, 8}, opt);
    check(tr.is_folded(doc.entries[1]) && tr.layout_of(1)->lines.size() == 1, "and toggles it back");
    tr.scroll_to_top();
    tr.layout(doc, {0, 0, 30, 1}, opt);  // a one-row viewport showing only the prompt
    check(!tr.handle(ctrl('o'), doc, 3000), "Ctrl-O with no summary line in view does nothing");
  }

  // ---- hit-testing and a drag inside one entry ----
  {
    Document doc;
    doc.entries.push_back(user("u", "hello world"));
    Transcript tr;
    std::string copied;
    int copies = 0;
    tr.on_copy = [&](const std::string& s) { copied = s; ++copies; };
    tr.layout(doc, {0, 0, 20, 3}, opt);
    auto p = tr.hit(2, 0);
    check(p && *p == TextPos{0, 0, 1}, "the cell under 'h' is (entry 0, offset 0, 1 byte)");
    p = tr.hit(0, 0);
    check(p && *p == TextPos{0, 0, 0}, "a chrome cell (the prompt prefix) maps to the nearest text: the start of 'hello'");
    p = tr.hit(19, 0);
    check(p && *p == TextPos{0, 10, 1}, "past the end of the line: the last grapheme, inclusive");
    p = tr.hit(3, 2);
    check(p && *p == TextPos{0, 11, 0}, "a row below the text: the end of the last entry");
    tr.handle(mouse(MouseEvent::Kind::Press, 2, 0), doc, 1000);
    check(tr.selection().active && tr.selected_text() == "h", "a press selects the grapheme under the pointer");
    tr.handle(mouse(MouseEvent::Kind::Drag, 6, 0), doc, 1050);
    check(tr.selected_text() == "hello", "dragging right includes the cell under the pointer: 'hello'");
    tr.handle(mouse(MouseEvent::Kind::Drag, 0, 0), doc, 1100);
    check(tr.selected_text() == "h", "dragging back left onto chrome shrinks to the anchor grapheme");
    tr.handle(mouse(MouseEvent::Kind::Drag, 6, 0), doc, 1150);
    tr.handle(mouse(MouseEvent::Kind::Release, 6, 0), doc, 1200);
    check(copies == 1 && copied == "hello", "release copies on select (" + copied + ")");
    Frame f(20, 3);
    tr.draw(f, theme);
    const Style sel = theme.style(Role::selection);
    const bool selected_bg = sel.bg.kind != Color::Kind::None;
    check(selected_bg && f.at(2, 0).style.bg == sel.bg && f.at(6, 0).style.bg == sel.bg && f.at(7, 0).style.bg != sel.bg,
          "the selected cells wear the selection background; the next cell does not");
    check(f.at(0, 0).style.bg != sel.bg, "the prefix (chrome) is not highlighted when the line's text is only partly selected");
    check(tr.handle(alt('c'), doc, 1300) && copies == 2, "Alt-C copies again");
    check(tr.handle(key(Key::Escape), doc, 1400) && !tr.selection().active, "Escape clears the selection");
    check(!tr.handle(key(Key::Escape), doc, 1500), "and is not consumed when there is none");
    // A plain click (no drag) selects nothing.
    tr.handle(mouse(MouseEvent::Kind::Press, 4, 0), doc, 2000);
    tr.handle(mouse(MouseEvent::Kind::Release, 4, 0), doc, 2010);
    check(!tr.selection().active && copies == 2, "a click without a drag selects nothing and copies nothing");
    // Dragging leftwards from the anchor also includes the grapheme under the pointer.
    tr.handle(mouse(MouseEvent::Kind::Press, 6, 0), doc, 3000);
    tr.handle(mouse(MouseEvent::Kind::Drag, 2, 0), doc, 3050);
    check(tr.selected_text() == "hello", "dragging leftwards: the pointer's cell is included too");
    tr.handle(mouse(MouseEvent::Kind::Release, 2, 0), doc, 3100);
  }

  // ---- selection across a wrap and across entries: no wrap artefacts, bullets kept ----
  {
    Document doc;
    doc.entries.push_back(verbatim("v", "alpha beta gamma delta"));
    doc.entries.push_back(md("m", "- item one\n- item two"));
    Transcript tr;
    std::string copied;
    tr.on_copy = [&](const std::string& s) { copied = s; };
    tr.layout(doc, {0, 0, 12, 5}, opt);
    Frame f(12, 5);
    tr.draw(f, theme);
    check(row_text(f, 0) == "alpha beta" && row_text(f, 1) == "gamma delta" && row_text(f, 3) == "\xE2\x80\xA2 item one",
          "the fixture wraps as expected: [" + row_text(f, 0) + "|" + row_text(f, 1) + "|" + row_text(f, 3) + "]");
    tr.handle(mouse(MouseEvent::Kind::Press, 6, 0), doc, 1000);
    tr.handle(mouse(MouseEvent::Kind::Drag, 9, 4), doc, 1100);
    tr.handle(mouse(MouseEvent::Kind::Release, 9, 4), doc, 1200);
    check(copied == "beta gamma delta\n\xE2\x80\xA2 item one\n\xE2\x80\xA2 item two",
          "the copy is logical text: no break where the wrap was, entries joined by one newline, bullets kept [" + copied + "]");
    auto gap = tr.hit(3, 2);
    check(gap && *gap == TextPos{0, 22, 0}, "a gap row maps to the end of the previous entry");
    // Double-click a word, triple-click the logical line.
    tr.handle(mouse(MouseEvent::Kind::Press, 2, 1), doc, 5000);
    tr.handle(mouse(MouseEvent::Kind::Release, 2, 1), doc, 5010);
    tr.handle(mouse(MouseEvent::Kind::Press, 2, 1), doc, 5100);
    check(tr.selected_text() == "gamma", "a double-click selects the UAX #29 word under the pointer: [" + tr.selected_text() + "]");
    tr.handle(mouse(MouseEvent::Kind::Release, 2, 1), doc, 5110);
    check(copied == "gamma", "and the release copies it");
    tr.handle(mouse(MouseEvent::Kind::Press, 2, 1), doc, 5200);
    check(tr.selected_text() == "alpha beta gamma delta", "a third click selects the logical line — the whole unwrapped paragraph");
    tr.handle(mouse(MouseEvent::Kind::Release, 2, 1), doc, 5210);
    tr.handle(mouse(MouseEvent::Kind::Press, 2, 1), doc, 9000);  // too late to pair
    check(tr.selected_text() == "m", "a press after the pairing window starts over (one grapheme, the 'm' under x=2)");
    tr.handle(mouse(MouseEvent::Kind::Release, 2, 1), doc, 9010);
    // A double-click followed by a drag grows the selection by whole words.
    tr.handle(mouse(MouseEvent::Kind::Press, 2, 1), doc, 12000);
    tr.handle(mouse(MouseEvent::Kind::Release, 2, 1), doc, 12010);
    tr.handle(mouse(MouseEvent::Kind::Press, 2, 1), doc, 12100);
    tr.handle(mouse(MouseEvent::Kind::Drag, 7, 1), doc, 12200);  // into "delta"
    check(tr.selected_text() == "gamma delta", "dragging after a double-click extends by whole words: [" + tr.selected_text() + "]");
    tr.handle(mouse(MouseEvent::Kind::Drag, 7, 0), doc, 12300);  // back up into "beta"
    check(tr.selected_text() == "beta gamma", "and backwards from the word's end: [" + tr.selected_text() + "]");
    tr.handle(mouse(MouseEvent::Kind::Release, 7, 0), doc, 12400);
    check(copied == "beta gamma", "release copies the word-extended selection");
    tr.handle(mouse(MouseEvent::Kind::Release, 2, 1), doc, 9010);
    // Shift+press extends.
    tr.handle(mouse(MouseEvent::Kind::Press, 0, 0), doc, 20000);
    tr.handle(mouse(MouseEvent::Kind::Drag, 4, 0), doc, 20050);
    tr.handle(mouse(MouseEvent::Kind::Release, 4, 0), doc, 20100);
    tr.handle(mouse(MouseEvent::Kind::Press, 3, 1, true), doc, 21000);
    check(tr.selected_text() == "alpha beta gamm", "Shift+press extends the selection to the pointer: [" + tr.selected_text() + "]");
    tr.handle(mouse(MouseEvent::Kind::Release, 3, 1, true), doc, 21100);
  }

  // ---- edge auto-scroll while dragging ----
  {
    Document doc;
    doc.entries.push_back(verbatim("n", lines(30)));
    Transcript tr;
    std::string copied;
    tr.on_copy = [&](const std::string& s) { copied = s; };
    tr.layout(doc, {0, 2, 20, 5}, opt);  // the area starts at row 2, so edges are not row 0
    tr.scroll_to_top();
    tr.handle(mouse(MouseEvent::Kind::Press, 0, 2), doc, 1000);  // "line 1"
    check(!tr.wants_tick(), "no ticks wanted while the pointer is inside");
    tr.handle(mouse(MouseEvent::Kind::Drag, 0, 9), doc, 1050);  // three rows past the bottom edge (row 6)
    check(tr.wants_tick(), "a drag past the bottom edge asks for ticks");
    check(tr.selected_text() == "line 1\nline 2\nline 3\nline 4\nl", "meanwhile the head sits on the last visible row");
    tr.tick();
    check(tr.top_line() == 3, "one tick scrolls by the distance past the edge (3 rows): top " + std::to_string(tr.top_line()));
    check(tr.selected_text() == "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nl", "and the head follows to the new last row");
    tr.tick();
    check(tr.top_line() == 6, "ticks keep scrolling while the pointer stays out");
    tr.handle(mouse(MouseEvent::Kind::Drag, 0, 4), doc, 1300);
    check(!tr.wants_tick(), "back inside: no more ticks");
    tr.handle(mouse(MouseEvent::Kind::Drag, 0, 0), doc, 1350);  // two rows above the top edge
    check(tr.wants_tick(), "past the top edge asks for ticks too");
    tr.tick();
    check(tr.top_line() == 4, "and scrolls up by the distance (2): top " + std::to_string(tr.top_line()));
    tr.handle(mouse(MouseEvent::Kind::Release, 0, 0), doc, 1400);
    check(!copied.empty() && copied.find("line 1") == 0, "release after an auto-scrolled drag copies the span");
    check(!tr.scroll().follow, "auto-scrolling up leaves follow off");
    // Dragging to the very bottom re-engages follow.
    tr.handle(mouse(MouseEvent::Kind::Press, 0, 2), doc, 3000);
    tr.handle(mouse(MouseEvent::Kind::Drag, 0, 60), doc, 3050);
    for (int i = 0; i < 40 && tr.wants_tick(); ++i) tr.tick();
    check(tr.scroll().follow && tr.top_line() == tr.total_lines() - 5, "a drag held past the bottom reaches the end and follow re-engages");
    tr.handle(mouse(MouseEvent::Kind::Release, 0, 60), doc, 3100);
  }

  // ---- hyperlinks: OSC 8 from the parsed URL; chrome never copies ----
  {
    Document doc;
    doc.entries.push_back(md("m", "see [docs](https://example.com/d) now\n\n```sh\nmake\n```"));
    Transcript tr;
    tr.layout(doc, {0, 0, 40, 6}, opt);
    Frame f(40, 6);
    tr.draw(f, theme);
    int linked = 0;
    std::string url;
    for (int x = 0; x < 40; ++x)
      if (f.at(x, 0).link != 0) { ++linked; url = std::string(f.link(f.at(x, 0).link)); }
    check(linked == 4 && url == "https://example.com/d", "exactly the link text's 4 cells carry the parsed URL (" + std::to_string(linked) + ", " + url + ")");
    std::string bytes = render_full(f, ColorDepth::TrueColor);
    const std::size_t open = bytes.find("\x1b]8;;https://example.com/d\x1b\\");
    const std::size_t glyphs = bytes.find("docs\x1b]8;;\x1b\\");
    check(open != std::string::npos && glyphs != std::string::npos && open < glyphs && bytes.find("docs") == glyphs,
          "the diff opens the link before its first glyph (the link's SGR may follow the open) and closes it right after the last");
    Frame plain(40, 6);
    plain.put_text(0, 0, "no links here", theme.style(Role::text), 40);
    check(render_full(plain, ColorDepth::TrueColor).find("]8;;") == std::string::npos, "a frame without links emits no OSC 8");
    // A changed link is a changed cell.
    Frame g = f;
    g.put_text(4, 0, "docs", theme.style(Role::md_link), 4, false, g.link_id("https://other/"));
    check(render_diff(&f, g, ColorDepth::TrueColor).find("https://other/") != std::string::npos, "a link change alone redraws the cell");
    // Select everything: the code box's rules and bars never reach the copy.
    tr.handle(mouse(MouseEvent::Kind::Press, 0, 0), doc, 1000);
    tr.handle(mouse(MouseEvent::Kind::Drag, 39, 5), doc, 1100);
    std::string all = tr.selected_text();
    tr.handle(mouse(MouseEvent::Kind::Release, 39, 5), doc, 1200);
    check(all.find("see docs (https://example.com/d) now") == 0 && all.find("make") != std::string::npos,
          "the whole entry's logical text: link text with its URL, then the code line [" + all + "]");
    check(all.find("\xE2\x94\x80") == std::string::npos && all.find("\xE2\x94\x82") == std::string::npos,
          "no box-drawing chrome in the copy");
    f = Frame(40, 6);
    tr.draw(f, theme);
    const Style sel = theme.style(Role::selection);
    check(f.at(0, 3).style.bg == sel.bg, "the code box's border highlights with a wholly selected line");
  }

  // ---- degenerate inputs ----
  {
    Document empty;
    Transcript tr;
    tr.layout(empty, {0, 0, 10, 3}, opt);
    Frame f(10, 3);
    tr.draw(f, theme);
    check(tr.total_lines() == 0 && !tr.hit(0, 0) && !tr.handle(mouse(MouseEvent::Kind::Press, 0, 0), empty, 0) == false,
          "an empty document: no lines, no hit, a press is harmless");
    check(!tr.selection().active, "and no selection");
    Document doc;
    doc.entries.push_back(md("m", "x"));
    tr.layout(doc, {0, 0, 0, 0}, opt);
    tr.draw(f, theme);
    tr.scroll_by(5);
    tr.scroll_page(-1);
    check(tr.total_lines() == 1 && tr.top_line() == 0, "a zero-size area lays out without dividing by anything");
    TranscriptOptions inset = opt;
    inset.inset = 1;
    tr.layout(doc, {0, 0, 2, 1}, inset);
    check(tr.text_area().w == 2, "an inset that would leave no column is not applied");
    tr.layout(doc, {0, 0, 3, 1}, inset);
    check(tr.text_area().x == 1 && tr.text_area().w == 1, "an inset of 1 on a 3-wide area leaves one column");
  }

  // ---- FIND (Phase 12 m4) ------------------------------------------------------------
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

    Document doc;
    doc.entries.push_back(user("u1", "find the needle"));
    doc.entries.push_back(md("a1", "A needle in a paragraph, and another needle after it."));
    doc.entries.push_back(tool("t1", "read_file haystack.txt", "one\nthe needle is in here\nthree"));
    doc.entries.push_back(verbatim("n1", "no match on this line"));

    Transcript tr;
    tr.layout(doc, {0, 0, 40, 8}, opt);
    check(tr.match_count() == 0 && tr.current_match_number() == 0 && tr.query().empty(),
          "no query: no matches, no current, nothing to render");

    // (a) A FOLDED entry is searched UNFOLDED, so the count does not depend on which
    // blocks happen to be open — the whole reason searchable_text exists.
    check(tr.is_folded(doc.entries[2]), "the tool entry starts folded");
    tr.set_query("needle");
    tr.layout(doc, {0, 0, 40, 8}, opt);
    check(tr.match_count() == 4,
          "four matches: one in the prompt, two in the answer, and ONE INSIDE THE FOLDED BLOCK (" +
              std::to_string(tr.match_count()) + ")");
    const std::size_t folded_hits = [&] {
      std::size_t n = 0;
      for (const FindMatch& m : tr.matches()) if (m.entry == 2) ++n;
      return n;
    }();
    check(folded_hits == 1, "…and it is attributed to the folded entry, not to its summary");

    // (b) Revealing a match inside a folded block UNFOLDS it.
    // BOUNDED by the match count on purpose. Written unbounded first, it HUNG under this
    // milestone's own negative control (which removes the folded-block match, so the
    // cycle never reaches entry 2) — and a test that hangs cannot tell you what it found,
    // exactly as CLAUDE.md says of one that crashes. The bound makes the control fail
    // with a named assertion instead.
    for (std::size_t step = 0; step <= tr.match_count() && tr.current_match() && tr.current_match()->entry != 2; ++step) {
      tr.find_next();
      tr.layout(doc, {0, 0, 40, 8}, opt);
    }
    check(tr.current_match() && tr.current_match()->entry == 2, "stepped to the match inside the folded block");
    check(!tr.is_folded(doc.entries[2]), "…and revealing it UNFOLDED the block");
    {
      Frame f(40, 8);
      tr.draw(f, theme);
      check(!find_rows(f, current_bg).empty(), "…the current match is on screen and painted in find_current");
    }

    // (c) The count and the position are readable, and stepping wraps.
    tr.set_query("needle");  // same query: a no-op, current match kept
    const std::size_t at = tr.current_match_number();
    check(at >= 1 && at <= 4, "the current match has a 1-based position for a host to print (" + std::to_string(at) + "/4)");
    for (std::size_t i = 0; i < 4; ++i) { tr.find_next(); tr.layout(doc, {0, 0, 40, 8}, opt); }
    check(tr.current_match_number() == at, "four find_next on four matches wraps exactly back to where it started");
    tr.find_prev();
    tr.layout(doc, {0, 0, 40, 8}, opt);
    check(tr.current_match_number() == (at == 1 ? 4 : at - 1), "find_prev steps back and wraps the other way");

    // (c2) The OTHER matches paint too, in find_match and not in find_current — the two
    // roles are what make "which of the four am I on" answerable, so both must be on
    // screen at once for the claim to mean anything.
    {
      Document two;
      two.entries.push_back(verbatim("p", "needle one\nneedle two"));
      Transcript trm;
      trm.layout(two, {0, 0, 20, 4}, opt);
      trm.set_query("needle");
      trm.layout(two, {0, 0, 20, 4}, opt);
      Frame f(20, 4);
      trm.draw(f, theme);
      const std::vector<int> cur = find_rows(f, current_bg);
      const std::vector<int> oth = find_rows(f, match_bg);
      check(trm.match_count() == 2 && cur.size() == 1 && oth.size() == 1 && cur[0] != oth[0],
            "with two matches in view, exactly one row carries find_current and the other find_match (" +
                std::to_string(cur.size()) + "/" + std::to_string(oth.size()) + ")");
    }

    // (d) ASCII-case-insensitive, and non-overlapping.
    tr.set_query("NEEDLE");
    tr.layout(doc, {0, 0, 40, 8}, opt);
    check(tr.match_count() == 4, "the search is ASCII-case-insensitive (Transcript.hpp states it rather than inferring it)");
    Document aaa;
    aaa.entries.push_back(verbatim("r", "aaaa"));
    Transcript tr2;
    tr2.layout(aaa, {0, 0, 20, 4}, opt);
    tr2.set_query("aa");
    tr2.layout(aaa, {0, 0, 20, 4}, opt);
    check(tr2.match_count() == 2, "matches do not overlap: 'aa' in 'aaaa' is two, not three");

    // (e) A match that WRAPS across two rows highlights on both — the property that
    // comes free from matching logical text and testing each cell's source offset.
    Document wrapped;
    wrapped.entries.push_back(verbatim("w", "zz aaaaaaaaaaaaaaaa zz"));
    Transcript tr3;
    tr3.layout(wrapped, {0, 0, 8, 6}, opt);
    tr3.set_query("aaaaaaaaaaaa");  // 12 a's: cannot fit on one 8-cell row
    tr3.layout(wrapped, {0, 0, 8, 6}, opt);
    check(tr3.match_count() == 1, "one match, longer than the row is wide");
    {
      Frame f(8, 6);
      tr3.draw(f, theme);
      const std::vector<int> rows = find_rows(f, current_bg);
      check(rows.size() >= 2, "…and it is highlighted on BOTH rows it wrapped onto (" + std::to_string(rows.size()) + ")");
    }

    // (f) An empty query clears without moving the view.
    Document many;
    for (int i = 0; i < 12; ++i) many.entries.push_back(verbatim(("e" + std::to_string(i)).c_str(), "needle " + std::to_string(i)));
    Transcript tr4;
    tr4.layout(many, {0, 0, 20, 4}, opt);
    tr4.scroll_to_top();
    tr4.scroll_by(6);
    tr4.layout(many, {0, 0, 20, 4}, opt);
    const std::size_t before = tr4.top_line();
    tr4.set_query("");
    tr4.layout(many, {0, 0, 20, 4}, opt);
    check(tr4.top_line() == before && tr4.match_count() == 0 && tr4.current_match_number() == 0,
          "an empty query clears the matches and does NOT move the view (" + std::to_string(tr4.top_line()) + " vs " +
              std::to_string(before) + ")");
    // …while a real query does move it, so the check above is about EMPTY and not about
    // find never scrolling (the negative control for it).
    tr4.set_query("needle 11");
    tr4.layout(many, {0, 0, 20, 4}, opt);
    check(tr4.top_line() != before && tr4.match_count() == 1, "a query with a match further down does scroll to it");

    // (g) The selection wins where they overlap (Transcript.hpp's stated precedence).
    Document one;
    one.entries.push_back(verbatim("s", "needle"));
    Transcript tr5;
    tr5.layout(one, {0, 0, 20, 3}, opt);
    tr5.set_query("needle");
    tr5.layout(one, {0, 0, 20, 3}, opt);
    {
      Frame f(20, 3);
      tr5.draw(f, theme);
      check(!find_rows(f, current_bg).empty(), "the match paints before anything is selected");
    }
    tr5.select({0, 0, 1}, {0, 5, 1});
    {
      Frame f(20, 3);
      tr5.draw(f, theme);
      check(f.at(0, 0).style.bg == theme.style(Role::selection).bg,
            "…and a selection over it wins: the user's most recent direct act is what the cell says");
    }

    // (h) The standing degenerate-size rule, extended to find.
    Transcript tr6;
    tr6.set_query("needle");
    tr6.layout(one, {0, 0, 0, 0}, opt);
    Frame f0(1, 1);
    tr6.draw(f0, theme);
    tr6.find_next();
    tr6.layout(one, {0, 0, 1, 1}, opt);
    tr6.draw(f0, theme);
    check(tr6.match_count() == 1, "a 0- and a 1-cell area still find, still draw, still step");
  }

  return report("rolltui transcript_test");
}
