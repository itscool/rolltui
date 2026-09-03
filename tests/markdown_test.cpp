//
// markdown_test.cpp — the markdown renderer (rolltui/Markdown.hpp) over vendored md4c.
//
// The load-bearing assertion is "never drops a character": for every fixture under
// rolltui/tests/fixtures/md/ (real model-written markdown copied from this repo's own
// journal and plan, plus an unterminated fence, an over-wide table and a nested-list
// torture case) and at widths 8, 20, 40, 80, 120, the non-space graphemes of the
// rendered plain text contain, in order, every non-space grapheme md4c's text
// callbacks reported. The renderer may add markers, borders and URLs; it may never
// lose model output. Plus: no line wider than the width, and shape checks on the
// individual constructs.
//
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>

#include "rolltui/Diff.hpp"
#include "rolltui/Markdown.hpp"
#include "rolltui/Unicode.hpp"
#include "rolltui/third_party/md4c/md4c.h"
#include "rolltui_test.hpp"

using namespace rolltui;
using namespace rolltui::markdown;
using namespace rolltui_test;

#ifndef ROLLTUI_FIXTURE_DIR
#error "ROLLTUI_FIXTURE_DIR must point at rolltui/tests/fixtures"
#endif

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Every chunk md4c reports as text, in order, gathered independently of the renderer
// (a second parse with our own callbacks — the reference, not the product), and
// whether the document holds a table (whose grid interleaves cells across lines, so
// order is checked per chunk there rather than globally).
struct Collected {
  std::vector<std::string> chunks;
  bool has_table = false;
};
int c_enter_block(MD_BLOCKTYPE t, void*, void* ud) {
  if (t == MD_BLOCK_TABLE) static_cast<Collected*>(ud)->has_table = true;
  return 0;
}
int c_leave_block(MD_BLOCKTYPE, void*, void*) { return 0; }
int c_enter_span(MD_SPANTYPE, void*, void*) { return 0; }
int c_leave_span(MD_SPANTYPE, void*, void*) { return 0; }
int c_text(MD_TEXTTYPE type, const MD_CHAR* t, MD_SIZE n, void* ud) {
  Collected& c = *static_cast<Collected*>(ud);
  std::string_view s(t, n);
  if (type == MD_TEXT_NULLCHAR) c.chunks.emplace_back("\xEF\xBF\xBD");
  else if (type == MD_TEXT_ENTITY) c.chunks.push_back(decode_entity(s));
  else if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) c.chunks.emplace_back(" ");
  else c.chunks.emplace_back(s);
  return 0;
}
Collected md4c_text(const std::string& src) {
  Collected c;
  MD_PARSER p{};
  p.flags = parser_flags();
  p.enter_block = c_enter_block; p.leave_block = c_leave_block;
  p.enter_span = c_enter_span; p.leave_span = c_leave_span;
  p.text = c_text;
  md_parse(src.data(), static_cast<MD_SIZE>(src.size()), &p, &c);
  return c;
}

std::vector<std::string> nonspace_graphemes(const std::string& s) {
  std::vector<std::string> v;
  for (const unicode::Grapheme& g : unicode::graphemes(s)) {
    std::string t(s.substr(g.offset, g.length));
    if (t == " " || t == "\n" || t == "\t" || t == "\r" || t == "\xC2\xA0") continue;
    if (unicode::display_width(t) == 0) continue;
    v.push_back(t);
  }
  return v;
}

// Is `needle` a subsequence of `hay`? Reports the first missing element.
std::string subsequence_gap(const std::vector<std::string>& needle, const std::vector<std::string>& hay) {
  std::size_t h = 0;
  for (std::size_t i = 0; i < needle.size(); ++i) {
    while (h < hay.size() && hay[h] != needle[i]) ++h;
    if (h == hay.size()) return "grapheme #" + std::to_string(i) + " [" + needle[i] + "] never rendered in order";
    ++h;
  }
  return "";
}

std::string never_drops(const std::string& src, int width) {
  std::vector<StyledLine> lines = render(src, RenderOptions{.width = width});
  std::string plain = plain_text(lines);
  Collected ref = md4c_text(src);
  std::vector<std::string> rendered = nonspace_graphemes(plain);
  // 1. Never drops: every grapheme is drawn at least as often as md4c reported it.
  {
    std::map<std::string, long> count;
    for (const std::string& g : rendered) ++count[g];
    std::string all;
    for (const std::string& c : ref.chunks) all += c;
    for (const std::string& g : nonspace_graphemes(all))
      if (--count[g] < 0) return "grapheme [" + g + "] rendered fewer times than the source has it";
  }
  // 2. In order: globally when the document has no table; per text chunk otherwise
  //    (a table's grid interleaves cells across lines, which is layout, not loss).
  if (!ref.has_table) {
    std::string all;
    for (const std::string& c : ref.chunks) all += c;
    std::string gap = subsequence_gap(nonspace_graphemes(all), rendered);
    if (!gap.empty()) return gap;
  } else {
    for (const std::string& c : ref.chunks) {
      std::string gap = subsequence_gap(nonspace_graphemes(c), rendered);
      if (!gap.empty()) return "chunk [" + c + "]: " + gap;
    }
  }
  for (const StyledLine& l : lines) {
    int w = 0;
    for (const Span& s : l.spans) w += s.width;
    if (w != l.width) return "StyledLine::width disagrees with its spans";
    if (l.width - width > 1)  // the wrap engine's single-oversized-grapheme allowance
      return "line exceeds width by " + std::to_string(l.width - width) + ": [" + plain_text({l}) + "]";
  }
  return "";
}

std::vector<std::string> lines_of(const std::vector<StyledLine>& v) {
  std::vector<std::string> out;
  for (const StyledLine& l : v) out.push_back(plain_text({l}));
  return out;
}

bool has_role(const std::vector<StyledLine>& v, Role r, const std::string& text) {
  for (const StyledLine& l : v)
    for (const Span& s : l.spans)
      if (s.role == r && s.text.find(text) != std::string::npos) return true;
  return false;
}

// A two-language ("cpp", "python") toy highlighter — the seam's PROOF, not a shipped
// highlighter (Markdown.hpp: "the library ships no highlighter"; a real one is
// deliberately out of scope, plan/phase-12.md m2's "Deliberately NOT in this phase").
// It returns byte-range SPANS only, never text and never a painter: the renderer
// alone decides how those bytes wrap and land in the cell grid. `calls`, when given,
// counts every invocation, so a test can assert exactly when the renderer does — and
// does not — call it.
Highlighter toy_highlighter(int* calls = nullptr) {
  return [calls](std::string_view lang, std::string_view line) -> std::vector<HighlightSpan> {
    if (calls) ++*calls;
    std::vector<HighlightSpan> spans;
    auto mark_all = [&](std::string_view word, Role role) {
      std::size_t pos = 0;
      while ((pos = line.find(word, pos)) != std::string_view::npos) {
        spans.push_back({pos, pos + word.size(), role});
        pos += word.size();
      }
    };
    if (lang == "cpp") {
      mark_all("int", Role::accent_1);
      mark_all("return", Role::accent_1);
    } else if (lang == "python") {
      mark_all("def", Role::accent_2);
      mark_all("return", Role::accent_2);
    }
    return spans;
  };
}

}  // namespace

int main() {
  // ---- fixtures: never drop a character, at several widths -----------------------
  std::string dir = std::string(ROLLTUI_FIXTURE_DIR) + "/md";
  std::vector<std::string> fixtures;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      std::string n = e->d_name;
      if (n.size() > 3 && n.substr(n.size() - 3) == ".md") fixtures.push_back(n);
    }
    closedir(d);
  }
  std::sort(fixtures.begin(), fixtures.end());
  check(fixtures.size() >= 8, "found the fixtures (" + std::to_string(fixtures.size()) + ")");
  for (const std::string& f : fixtures) {
    std::string src = read_file(dir + "/" + f);
    check(!src.empty(), f + " is non-empty");
    for (int w : {8, 20, 40, 80, 120}) {
      std::string err = never_drops(src, w);
      check(err.empty(), f + " @ " + std::to_string(w) + ": never drops a character, never overflows" +
                             (err.empty() ? "" : " — " + err));
    }
  }

  // ---- constructs ----------------------------------------------------------------
  {
    auto v = render("# Title\n\nA paragraph with *em*, **strong**, `code`, ~~gone~~ and a [link](https://x.y/z).\n",
                    RenderOptions{.width = 80});
    auto L = lines_of(v);
    check(L.size() == 3 && L[0] == "# Title" && L[1].empty(), "heading, blank line, paragraph");
    check(has_role(v, Role::md_heading, "# Title"), "heading role");
    check(has_role(v, Role::md_emphasis, "em") && has_role(v, Role::md_strong, "strong") &&
              has_role(v, Role::md_code_inline, "code") && has_role(v, Role::md_strikethrough, "gone"),
          "inline roles: emphasis, strong, code, strikethrough");
    check(has_role(v, Role::md_link, "link") && has_role(v, Role::md_link_url, "(https://x.y/z)"),
          "link text in md_link, URL shown after it in md_link_url");
    check(L[2] == "A paragraph with em, strong, code, gone and a link (https://x.y/z).",
          "paragraph plain text: [" + L[2] + "]");
  }
  {
    auto v = render("An autolink https://example.com/p here.\n", RenderOptions{.width = 80});
    check(lines_of(v)[0] == "An autolink https://example.com/p here.", "autolink URL is not repeated");
  }
  {
    auto v = render("**bold across a wrap point that is long** enough", RenderOptions{.width = 20});
    check(v.size() >= 2 && has_role(v, Role::md_strong, "bold") && has_role(v, Role::md_strong, "long"),
          "inline style survives a wrap: strong on both lines");
  }
  {
    auto v = render("```cpp\nint x = 1;\n```\n", RenderOptions{.width = 30});
    auto L = lines_of(v);
    check(L.size() == 3, "fenced code: top rule, one line, bottom rule (" + std::to_string(L.size()) + ")");
    check(L[0].rfind("\xE2\x94\x8C cpp ", 0) == 0 && L[0].size() > 10, "code box top carries the info string: [" + L[0] + "]");
    check(L[1] == "\xE2\x94\x82 int x = 1;                 \xE2\x94\x82", "code line is padded to the box: [" + L[1] + "]");
    check(has_role(v, Role::md_code_block, "int x = 1;") && has_role(v, Role::md_code_label, "cpp"), "code roles");
    check(v[0].width == 30 && v[1].width == 30 && v[2].width == 30, "code box is exactly the width");
  }
  {
    auto v = render("Text before\n\n```python\ndef f():\n    return 1\n", RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 6 && L[2].rfind("\xE2\x94\x8C python", 0) == 0 && L[5].rfind("\xE2\x94\x94", 0) == 0,
          "an unterminated fence renders as a code block to the end");
    check(has_role(v, Role::md_code_block, "return 1"), "its last line is code");
  }
  // ---- highlighter seam (plan/phase-12.md m2) -------------------------------------
  {
    int calls = 0;
    RenderOptions ro{.width = 30};
    ro.highlight = toy_highlighter(&calls);
    auto v = render("```cpp\nint x = 1;\n```\n", ro);
    auto L = lines_of(v);
    check(calls == 1, "highlighter called exactly once for the block's one line (" + std::to_string(calls) + ")");
    check(has_role(v, Role::accent_1, "int"), "a registered highlighter colours 'int' with its returned role");
    check(has_role(v, Role::md_code_block, " x = 1;"), "bytes outside a span keep the base code role");
    check(L[1] == "\xE2\x94\x82 int x = 1;                 \xE2\x94\x82",
          "a highlighter changes ROLES only — wrapping and padding are exactly as before: [" + L[1] + "]");
  }
  {
    // A different fence language reaches the same callback and picks a different
    // keyword set: the seam threads the fence's own info string through, it is not a
    // fixed language.
    int calls = 0;
    RenderOptions ro{.width = 40};
    ro.highlight = toy_highlighter(&calls);
    auto v = render("```python\ndef f():\n    return 1\n```\n", ro);
    check(calls == 2, "called once per code line (" + std::to_string(calls) + ")");
    check(has_role(v, Role::accent_2, "def") && has_role(v, Role::accent_2, "return"),
          "python keywords take the highlighter's role");
    check(!has_role(v, Role::accent_1, "def"), "the cpp keyword set is not applied to a python block");
  }
  {
    // An HTML block carries no language tag (Markdown.hpp: "never for an HTML
    // block" — it is always opaque code, never interpreted).
    int calls = 0;
    RenderOptions ro{.width = 30};
    ro.highlight = toy_highlighter(&calls);
    render("<div>\n<b>raw</b>\n</div>\n", ro);
    check(calls == 0, "an HTML block is never handed to the highlighter");
  }
  {
    // The control: RenderOptions::highlight left UNSET. This is the seam's whole
    // mechanism for "the callback is never called for a line no theme role could
    // distinguish (a mono theme asks for no spans)" (plan/phase-12.md m2) — the
    // renderer cannot itself see a Theme (Markdown.hpp/Theme.hpp: roles are emitted,
    // never colours), so the guarantee it can make and this asserts is: nothing is
    // called when nothing is registered. A host under a mono theme asks for no spans
    // simply by not registering a highlighter, exactly as here.
    RenderOptions ro{.width = 30};  // .highlight default-constructed: unset
    Rendered r = render_text("```cpp\nint x = 1;\n```\n", ro);
    check(r.highlight_report.clean(), "an unregistered highlighter leaves the report clean");
    check(has_role(r.lines, Role::md_code_block, "int x = 1;"),
          "unregistered: the code line keeps exactly its pre-seam role");
    // This IS the "no highlighter leaves every existing markdown golden
    // byte-identical" control: every exact-string assertion elsewhere in this file
    // (e.g. the fenced-code and unterminated-fence cases above) runs with
    // RenderOptions::highlight left at this same default and must keep passing
    // unchanged — verified against a captured pre-seam dump of every fixture at
    // every tested width (JOURNAL.md).
  }
  {
    // ---- clamping: overlap, backwards, and past-the-line spans are corrected AND
    // NAMED, never silently dropped (CLAUDE.md: a control that reports zero has been
    // wrong before — an empty report must mean nothing needed clamping, not that
    // clamping was skipped).
    RenderOptions ro{.width = 30};
    ro.highlight = [](std::string_view, std::string_view line) -> std::vector<HighlightSpan> {
      std::vector<HighlightSpan> spans;
      spans.push_back({0, 3, Role::accent_1});                              // "int" — well-formed
      spans.push_back({1, 5, Role::accent_2});                              // overlaps the first
      spans.push_back({5, 2, Role::accent_3});                              // runs backwards
      spans.push_back({line.size() - 2, line.size() + 50, Role::accent_4});  // exceeds the line
      return spans;
    };
    Rendered r = render_text("```cpp\nint x = 1;\n```\n", ro);  // the line is "int x = 1;", 10 bytes
    check(!r.highlight_report.clean(), "malformed spans are reported, not dropped silently");
    check(r.highlight_report.clamped.size() == 3,
          "each malformed span gets its own named entry (" + std::to_string(r.highlight_report.clamped.size()) + ")");
    std::string all;
    for (const std::string& m : r.highlight_report.clamped) all += m + "\n";
    check(all.find("runs backwards") != std::string::npos, "the backwards span is named: [" + all + "]");
    check(all.find("overlaps an earlier span") != std::string::npos, "the overlapping span is named: [" + all + "]");
    check(all.find("exceeds the line") != std::string::npos, "the past-the-line span is named: [" + all + "]");
    check(has_role(r.lines, Role::accent_1, "int"),
          "the one well-formed span still renders despite its malformed neighbours");
  }
  {
    auto v = render("<div>\n<b>raw</b>\n</div>\n", RenderOptions{.width = 30});
    check(has_role(v, Role::md_code_block, "<b>raw</b>") && has_role(v, Role::md_code_label, "html"),
          "an HTML block is shown as code, never interpreted");
  }
  {
    auto v = render("- one\n- two\n  - nested\n- three\n", RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 4 && L[0] == "\xE2\x80\xA2 one" && L[2] == "  \xE2\x80\xA2 nested" && L[3] == "\xE2\x80\xA2 three",
          "tight bullet list with a nested list: " + L[0] + " | " + L[2]);
    check(has_role(v, Role::md_list_marker, "\xE2\x80\xA2"), "bullet in md_list_marker");
  }
  {
    auto v = render("1. a\n2. b\n3. c\n", RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 3 && L[0] == "1. a" && L[2] == "3. c", "ordered list numbering");
    auto w = render("9. a\n10. b\n", RenderOptions{.width = 40});
    auto M = lines_of(w);
    check(M.size() == 2 && M[0] == " 9. a" && M[1] == "10. b", "ordered markers align on the widest number: [" + M[0] + "]");
  }
  {
    auto v = render("- [ ] todo\n- [x] done\n", RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 2 && L[0] == "[ ] todo" && L[1] == "[x] done", "task list markers");
  }
  {
    auto v = render("- a long item that wraps onto a second line for sure\n", RenderOptions{.width = 24});
    auto L = lines_of(v);
    check(L.size() >= 2 && L[0].rfind("\xE2\x80\xA2 ", 0) == 0 && L[1].rfind("  ", 0) == 0 && L[1][2] != ' ',
          "hanging indent under a bullet: [" + L[1] + "]");
  }
  {
    auto v = render("1. first\n\n   para two\n2. second\n", RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 5 && L[0] == "1. first" && L[1].empty() && L[2] == "   para two" && L[3].empty() && L[4] == "2. second",
          "loose list: blank lines between blocks and items");
  }
  {
    auto v = render("> quoted *text*\n> more\n", RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 1 && L[0] == "\xE2\x94\x82 quoted text more", "blockquote bar and joined soft break: [" + L[0] + "]");
    check(has_role(v, Role::md_quote, "quoted") && has_role(v, Role::md_emphasis, "text"), "quote base role, emphasis inside");
    auto n = render("> outer\n>\n> > inner\n", RenderOptions{.width = 40});
    auto N = lines_of(n);
    check(N.size() == 3 && N[1] == "\xE2\x94\x82" && N[2] == "\xE2\x94\x82 \xE2\x94\x82 inner", "nested quotes: [" + N[2] + "]");
  }
  {
    auto v = render("---\n", RenderOptions{.width = 10});
    auto L = lines_of(v);
    check(L.size() == 1 && v[0].width == 10 && has_role(v, Role::md_rule, "\xE2\x94\x80"), "thematic break spans the width");
  }
  {
    auto v = render("| a | b |\n|---|--:|\n| longer cell | 1 |\n", RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 5, "table: top, head, separator, body, bottom (" + std::to_string(L.size()) + ")");
    check(L.size() == 5 && L[1] == "\xE2\x94\x82 a           \xE2\x94\x82 b \xE2\x94\x82", "header row: [" + L[1] + "]");
    check(L.size() == 5 && L[3] == "\xE2\x94\x82 longer cell \xE2\x94\x82 1 \xE2\x94\x82", "right-aligned cell: [" + L[3] + "]");
    check(has_role(v, Role::md_table_header, "a") && has_role(v, Role::md_table_border, "\xE2\x94\x82"), "table roles");
  }
  {
    auto v = render("| alpha beta gamma | delta epsilon |\n|---|---|\n| one two three four | five |\n", RenderOptions{.width = 24});
    auto L = lines_of(v);
    bool fits = true;
    for (const StyledLine& l : v) fits &= (l.width <= 24);
    check(fits && L.size() > 5, "a wide table shrinks by wrapping cells (" + std::to_string(L.size()) + " lines)");
    check(never_drops("| alpha beta gamma | delta epsilon |\n|---|---|\n| one two three four | five |\n", 24).empty(),
          "…and keeps every character");
  }
  {
    std::string src = "| a | b | c | d | e |\n|---|---|---|---|---|\n| 1 | 2 | 3 | 4 | 5 |\n";
    auto v = render(src, RenderOptions{.width = 12});
    check(has_role(v, Role::md_code_label, "table") && has_role(v, Role::md_code_block, "| a |") &&
              has_role(v, Role::md_code_block, "---|"),
          "a table that cannot fit at one cell per column is shown as its source in a code block");
    check(never_drops(src, 12).empty(), "…and keeps every character");
  }
  {
    auto v = render("line one  \nline two\n", RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 2 && L[0] == "line one" && L[1] == "line two", "hard break splits the paragraph");
  }
  {
    std::string src = std::string("&amp; &lt;x&gt; &#65;&#x42; &copy; &bogus; a") + '\0' + "b";
    auto v = render(src, RenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L[0].find("& <x> AB \xC2\xA9 &bogus; a\xEF\xBF\xBD" "b") != std::string::npos, "entities decoded, unknown kept, NUL → U+FFFD: [" + L[0] + "]");
  }
  {
    auto v = render("![alt text](https://x.y/i.png)", RenderOptions{.width = 40});
    check(lines_of(v)[0] == "alt text (https://x.y/i.png)", "image renders as alt text plus URL");
  }
  {
    auto v = render("", RenderOptions{.width = 40});
    check(v.empty(), "empty source renders no lines");
    auto w = render("\n\n\n", RenderOptions{.width = 40});
    check(w.empty(), "blank source renders no lines");
  }
  {
    Document d = parse("# H\n\n- a\n\n```\nc\n```\n");
    check(d.blocks.size() == 3 && d.blocks[0].kind == Block::Kind::Heading && d.blocks[1].kind == Block::Kind::List &&
              d.blocks[2].kind == Block::Kind::Code && d.blocks[2].code == "c\n",
          "block tree shape");
  }
  // ---- the diff colouriser (plan/phase-12.md m5b) ----------------------------------
  // It is a Highlighter, so it rides m2's seam rather than being a second mechanism —
  // which is the cheapest rung and also the test of whether that seam was placed right.
  {
    auto role_of = [](std::string_view lang, std::string_view line) {
      const std::vector<HighlightSpan> sp = diff_spans(lang, line);
      return sp.empty() ? Role::count_ : sp[0].role;
    };
    check(role_of("diff", "+added") == Role::diff_added, "'+' is an added line");
    check(role_of("diff", "-gone") == Role::diff_removed, "'-' is a removed line");
    check(role_of("diff", " same") == Role::diff_context, "a leading space is context");
    check(role_of("diff", "") == Role::diff_context, "an empty line is context, not a change");
    check(role_of("diff", "@@ -1,4 +1,6 @@") == Role::accent_1, "a hunk header is a position, not a change");
    // The ORDER these are tested in is the bug they prevent: "+++ b/x" starts with '+'
    // and is not an added line, so the header test must come first. This looks right in
    // every screenshot that happens to start at a hunk.
    check(role_of("diff", "+++ b/file.txt") == Role::text_muted, "'+++' is a file header, NOT an added line");
    check(role_of("diff", "--- a/file.txt") == Role::text_muted, "'---' is a file header, NOT a removed line");
    check(role_of("diff", "\\ No newline at end of file") == Role::diff_context,
          "the no-newline note is context: colouring it as a change would lie about the file");
    // THE CONTROL, and the milestone's whole point: content is never sniffed. A block
    // whose CONTENT looks exactly like a diff renders plain under a bare fence.
    check(diff_spans("", "+added").empty() && diff_spans("python", "-x = 1").empty(),
          "a bare fence and another language get NO spans, whatever the content looks like");
    check(diff_spans("patch", "+x").size() == 1 && diff_spans("udiff", "+x").size() == 1,
          "'patch' and 'udiff' are the same claim as 'diff'");
    // A span covers the WHOLE line, marker included: a half-coloured line reads as a bug,
    // and the marker is doing separate work as the non-colour signal.
    const std::vector<HighlightSpan> sp = diff_spans("diff", "+abc");
    check(sp.size() == 1 && sp[0].begin == 0 && sp[0].end == 4, "the span covers the whole line, marker included");

    // Through the renderer, on the cells: the roles land, and the +/- prefixes SURVIVE —
    // which is what makes a diff readable with colour switched off (the Done-when).
    RenderOptions ro;
    ro.width = 40;
    ro.highlight = [](std::string_view lang, std::string_view line) { return diff_spans(lang, line); };
    const Rendered r = render_text("```diff\n@@ -1,2 +1,2 @@\n-old line\n+new line\n context\n```\n", ro);
    check(r.highlight_report.clean(), "the colouriser never produces a span the renderer has to clamp");
    bool has_added = false, has_removed = false, has_hunk = false, marks_kept = false;
    std::string all;
    for (const StyledLine& l : r.lines)
      for (const Span& x : l.spans) {
        all += x.text;
        if (x.role == Role::diff_added) has_added = true;
        if (x.role == Role::diff_removed) has_removed = true;
        if (x.role == Role::accent_1) has_hunk = true;
      }
    marks_kept = all.find("-old line") != std::string::npos && all.find("+new line") != std::string::npos;
    check(has_added && has_removed && has_hunk, "a ```diff fence colours through the diff roles");
    check(marks_kept, "…and the +/- prefixes are NEVER stripped: the signal a mono or CVD reader still has");
  }

  return report("rolltui markdown_test");
}
