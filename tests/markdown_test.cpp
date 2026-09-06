//
// markdown_test.cpp — the markdown renderer (rolltui/c/rolltui_markdown.h) over vendored
// md4c, calling the C API directly ahead of the C++ binding layer's deletion (Phase 17):
// `rolltui/Markdown.hpp` and its shim `Markdown.cpp` are gone from this file's includes,
// and every `rolltui::markdown::` type this test used is now its raw C equivalent, called
// through `rolltui/c/rolltui_markdown.h` and `rolltui/c/rolltui_md_lines.h`.
//
// SCOPE NOTE, stated rather than left implicit: `Role` (rolltui/Style.hpp) and the direct
// calls to `diff_spans` (rolltui/Diff.hpp) are deliberately NOT converted here. Neither is
// part of the Markdown shim this file replaces, and `Role` specifically has no C-side named
// equivalent to convert TO yet — `rolltui/c/rolltui_style.h` carries only the three ordinals
// that cross as struct-field defaults, and Style.hpp's own comment states the rest is a
// permanent C++-only vocabulary until Phase 17 m1 (unstarted: `plan/phase-17.md`) decides
// what replaces it. The ONE place `diff_spans` crosses into this file's OWN converted
// surface — plugged in as a `RenderOptions::highlight` — is bridged by a small local
// trampoline (`diff_highlight_fn`, below) that calls the existing, untouched C++ function
// and forwards its spans through the raw C sink; `diff_spans`'s own direct call sites
// (`role_of`, `one`, `spans_of`, `word_range_of`, …) are untouched.
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
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <dirent.h>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_markdown.h"
#include "rolltui/c/rolltui_md_lines.h"
#include "rolltui/third_party/md4c/md4c.h"
#include "rolltui_test.hpp"

using namespace rolltui_test;

#ifndef ROLLTUI_FIXTURE_DIR
#error "ROLLTUI_FIXTURE_DIR must point at rolltui/tests/fixtures"
#endif

namespace {

// THE ROLE ORDER, FROM THE LIBRARY — expanded from `ROLLTUI_ROLE_LIST` (Phase 17 m2a), not
// reproduced. This was one of SIX verbatim copies of the 49 names IN ORDER, each written when
// the role vocabulary was still C++ and a converted suite had no way to ask for it. A style
// table is indexed by this ordinal, so the order is ABI and a copy of it is a copy of ABI.
enum class Role : unsigned char {
#define ROLLTUI_TEST_ROLE_(lower, UPPER) lower,
  ROLLTUI_ROLE_LIST(ROLLTUI_TEST_ROLE_)
#undef ROLLTUI_TEST_ROLE_
  count_
};
static_assert(static_cast<unsigned char>(Role::text) == ROLLTUI_ROLE_DEFAULT_TEXT,
              "the C side's default entry role must be Role::text");
static_assert(static_cast<unsigned char>(Role::background) == ROLLTUI_ROLE_DEFAULT_BACKGROUND,
              "the C side's default node background must be Role::background");
static_assert(static_cast<unsigned char>(Role::prompt) == ROLLTUI_ROLE_DEFAULT_PROMPT,
              "the C side's default input prompt role must be Role::prompt");

// ---- mirrors the two Unicode.hpp functions this file used, over the C API directly
// (rolltui/c/rolltui_unicode.h) -- same helpers rolltui/tests/wrap_test.cpp already mirrors
// independently, since each converted test file stands alone. ----
RolltuiUnicodeScratch* u_scratch() {
  static std::unique_ptr<RolltuiUnicodeScratch, void (*)(RolltuiUnicodeScratch*)> s(rolltui_u_scratch_new(),
                                                                                    rolltui_u_scratch_free);
  return s.get();
}
std::vector<RolltuiUnicodeGrapheme> graphemes(std::string_view utf8) {
  std::vector<RolltuiUnicodeGrapheme> out(utf8.size());  // no more clusters than bytes
  const std::size_t n = rolltui_u_graphemes(u_scratch(), utf8.data(), utf8.size(), false, out.data());
  out.resize(n);
  return out;
}

// ---- mirrors rolltui::markdown::HighlightSpan (Markdown.hpp) and rolltui::diff_spans
// (Diff.hpp/Diff.cpp): the shape the diff colouriser answers in, and the colouriser itself,
// reproduced over rolltui/c/rolltui_diff.h -- both headers said "untouched" in this file's
// original scope note, which this conversion now closes out. `HighlightSpan` is not part of
// the (converted) Markdown shim; it is the one type that crosses the seam between it and
// this mirror, via `diff_highlight_fn` below. ----
struct HighlightSpan {
  std::size_t begin = 0;
  std::size_t end = 0;
  Role role = Role::md_code_block;
};

// THE ROLE TABLE, exactly Diff.cpp's kRoles: the C names no role at all, so the mapping
// lives in this one initializer, and markdown_test's own per-line-kind assertions are its
// oracle (swap two fields and they fail).
// PHASE 17 m3: this was a verbatim copy of `Diff.cpp`'s private table — two consumers writing
// the same thing, which means the API was wrong rather than the consumers. Both now read the
// library's `rolltui_diff_default_roles()`, so a changed mapping is one edit and this suite
// still fails on it (its per-line-kind assertions are the oracle either way).
// The block, read on demand: a BORROW of one line, never a copy (Diff.cpp's line_at).
const char* diff_line_at(const void* block, std::size_t i, std::size_t* len) {
  const std::string_view s = (*static_cast<const std::span<const std::string_view>*>(block))[i];
  *len = s.size();
  return s.data();
}
RolltuiDiffScratch* diff_scratch() {
  static std::unique_ptr<RolltuiDiffScratch, void (*)(RolltuiDiffScratch*)> s(rolltui_diff_scratch_new(),
                                                                              rolltui_diff_scratch_free);
  return s.get();
}
std::vector<HighlightSpan> diff_spans(std::string_view lang, std::span<const std::string_view> lines,
                                      std::size_t index) {
  // The bound is known WITHOUT asking, same as Diff.cpp: a line takes its whole role, or
  // splits into line / word / line.
  RolltuiDiffSpan buf[ROLLTUI_DIFF_MAX_SPANS];
  const std::size_t n = rolltui_diff_spans(diff_scratch(), lang.data(), lang.size(), &lines, lines.size(),
                                           diff_line_at, index, rolltui_diff_default_roles(), buf, ROLLTUI_DIFF_MAX_SPANS);
  std::vector<HighlightSpan> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) out.push_back({buf[i].begin, buf[i].end, static_cast<Role>(buf[i].role)});
  return out;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// rolltui_md_decode_entity's own size rule: the buffer must hold max(n, 4), because an
// entity it does not recognise is copied through verbatim rather than dropped.
std::string decode_entity(std::string_view entity) {
  std::string out(entity.size() < 4 ? 4 : entity.size(), '\0');
  out.resize(rolltui_md_decode_entity(entity.data(), entity.size(), out.data(), out.size()));
  return out;
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
  p.flags = rolltui_md_parser_flags();
  p.enter_block = c_enter_block; p.leave_block = c_leave_block;
  p.enter_span = c_enter_span; p.leave_span = c_leave_span;
  p.text = c_text;
  md_parse(src.data(), static_cast<MD_SIZE>(src.size()), &p, &c);
  return c;
}

std::vector<std::string> nonspace_graphemes(const std::string& s) {
  std::vector<std::string> v;
  for (const RolltuiUnicodeGrapheme& g : graphemes(s)) {
    std::string t(s.substr(g.offset, g.length));
    if (t == " " || t == "\n" || t == "\t" || t == "\r" || t == "\xC2\xA0") continue;
    if (rolltui_u_display_width(u_scratch(), t.data(), t.size(), false) == 0) continue;
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

// A render PLUS the storage its spans borrow from. `render()` used to hand back a
// `std::vector<StyledLine>` that owned every byte in it; since Phase 15 m4 a span owns
// nothing (rolltui_md_lines.h), so a caller has to keep the store alive for as long as it
// reads the lines. That obligation is the one thing the raw C API puts on a caller — in
// place of the deleted `rolltui::markdown::Rendered` shim, this holder OWNS the
// `RolltuiMdLines*` store directly and states the obligation rather than hiding it behind
// a copy — every assertion below is unchanged.
struct Lines {
  RolltuiMdLines* store = nullptr;
  std::span<const RolltuiMdLine> v;

  Lines() = default;
  Lines(const Lines&) = delete;
  Lines& operator=(const Lines&) = delete;
  Lines(Lines&& o) noexcept : store(o.store), v(o.v) { o.store = nullptr; o.v = {}; }
  Lines& operator=(Lines&& o) noexcept {
    if (this != &o) {
      rolltui_md_lines_free(store);
      store = o.store;
      v = o.v;
      o.store = nullptr;
      o.v = {};
    }
    return *this;
  }
  ~Lines() { rolltui_md_lines_free(store); }

  std::size_t size() const { return v.size(); }
  bool empty() const { return v.empty(); }
  const RolltuiMdLine& operator[](std::size_t i) const { return v[i]; }
  const RolltuiMdLine* begin() const { return v.data(); }
  const RolltuiMdLine* end() const { return v.data() + v.size(); }
  operator std::span<const RolltuiMdLine>() const { return v; }
  std::span<const RolltuiMdLine> lines() const { return v; }

  // The rest of what `Rendered` provided, called directly against the store.
  std::string_view text() const {
    return store ? std::string_view(rolltui_md_lines_text(store), rolltui_md_lines_text_size(store))
                : std::string_view{};
  }
  std::span<const RolltuiMdCodeBlock> code_blocks() const {
    const RolltuiMdCodeBlock* p = store ? rolltui_md_lines_code_blocks(store) : nullptr;
    return {p, p ? rolltui_md_lines_code_block_count(store) : 0};
  }
  std::size_t clamped_count() const { return store ? rolltui_md_lines_clamped_count(store) : 0; }
  std::string_view clamped(std::size_t i) const {
    const char* p = nullptr;
    std::size_t n = 0;
    rolltui_md_lines_clamped_at(store, i, &p, &n);
    return {p, n};
  }
  bool highlight_clean() const { return clamped_count() == 0; }
};

Lines render(std::string_view src, const RolltuiMdRenderOptions& opt = {}) {
  RolltuiMdRenderOptions o = opt;
  o.roles = *rolltui_md_roles();  // the shim's own patch, done here in its place
  RolltuiMdDoc* doc = rolltui_md_doc_new();
  rolltui_md_parse(doc, src.data(), src.size());
  Lines L;
  L.store = rolltui_md_lines_new();
  rolltui_md_render(L.store, doc, &o);
  rolltui_md_doc_free(doc);
  L.v = {rolltui_md_lines_all(L.store), rolltui_md_lines_count(L.store)};
  return L;
}

void plain_text_into(std::span<const RolltuiMdLine> lines, std::string& out) {
  out.clear();
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i) out += '\n';
    for (const RolltuiMdSpan& sp : lines[i].spans()) out.append(sp.text());
  }
}
std::string plain_text(std::span<const RolltuiMdLine> lines) {
  std::string s;
  plain_text_into(lines, s);
  return s;
}
std::string plain_text(const RolltuiMdLine& line) { return plain_text(std::span<const RolltuiMdLine>(&line, 1)); }

std::string summary(std::string_view lang, std::size_t lines, std::size_t bytes) {
  char buf[ROLLTUI_MD_SUMMARY_MAX];
  return std::string(buf, rolltui_md_code_block_summary(lang.data(), lang.size(), lines, bytes, buf, sizeof buf));
}

std::string never_drops(const std::string& src, int width) {
  auto lines = render(src, RolltuiMdRenderOptions{.width = width});
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
  for (const RolltuiMdLine& l : lines) {
    int w = 0;
    for (const RolltuiMdSpan& s : l.spans()) w += s.width;
    if (w != l.width) return "StyledLine::width disagrees with its spans";
    if (l.width - width > 1)  // the wrap engine's single-oversized-grapheme allowance
      return "line exceeds width by " + std::to_string(l.width - width) + ": [" + plain_text(l) + "]";
  }
  return "";
}

std::vector<std::string> lines_of(std::span<const RolltuiMdLine> v) {
  std::vector<std::string> out;
  for (const RolltuiMdLine& l : v) out.push_back(plain_text(l));
  return out;
}

bool has_role(std::span<const RolltuiMdLine> v, Role r, const std::string& text) {
  for (const RolltuiMdLine& l : v)
    for (const RolltuiMdSpan& s : l.spans())
      if (static_cast<Role>(s.role) == r && s.text().find(text) != std::string_view::npos) return true;
  return false;
}

// A two-language ("cpp", "python") toy highlighter — the seam's PROOF, not a shipped
// highlighter (Markdown.hpp: "the library ships no highlighter"; a real one is
// deliberately out of scope, plan/phase-12.md m2's "Deliberately NOT in this phase").
// Plugged in as `RolltuiMdRenderOptions::highlight` (a raw function pointer) with
// `highlight_ctx` carrying the `int*` call counter — there is no closure to capture into
// once the field is a C function pointer rather than a `std::function`. It emits byte-range
// SPANS only, through the sink, never text and never a painter: the renderer alone decides
// how those bytes wrap and land in the cell grid. `ctx`, when non-null, counts every
// invocation, so a test can assert exactly when the renderer does — and does not — call it.
void toy_highlight_fn(void* ctx, const char* lang, std::size_t lang_n, const RolltuiMdCodeLine* lines,
                      std::size_t /*line_count*/, std::size_t index, RolltuiMdSpanSink emit, void* sink) {
  int* calls = static_cast<int*>(ctx);
  if (calls) ++*calls;
  const std::string_view line(lines[index].p, lines[index].n);
  const std::string_view langv(lang, lang_n);
  auto mark_all = [&](std::string_view word, Role role) {
    std::size_t pos = 0;
    while ((pos = line.find(word, pos)) != std::string_view::npos) {
      emit(sink, pos, pos + word.size(), static_cast<unsigned char>(role));
      pos += word.size();
    }
  };
  if (langv == "cpp") {
    mark_all("int", Role::accent_1);
    mark_all("return", Role::accent_1);
  } else if (langv == "python") {
    mark_all("def", Role::accent_2);
    mark_all("return", Role::accent_2);
  }
}

// The clamping test's fixed malformed spans: overlap, backwards, and past-the-line, plus
// one well-formed span — emitted through the sink in this same fixed order.
void malformed_highlight_fn(void* /*ctx*/, const char* /*lang*/, std::size_t /*lang_n*/,
                            const RolltuiMdCodeLine* lines, std::size_t /*line_count*/, std::size_t index,
                            RolltuiMdSpanSink emit, void* sink) {
  const std::size_t n = lines[index].n;
  emit(sink, 0, 3, static_cast<unsigned char>(Role::accent_1));                      // "int" — well-formed
  emit(sink, 1, 5, static_cast<unsigned char>(Role::accent_2));                      // overlaps the first
  emit(sink, 5, 2, static_cast<unsigned char>(Role::accent_3));                      // runs backwards
  emit(sink, n - 2, n + 50, static_cast<unsigned char>(Role::accent_4));              // exceeds the line
}

// The bridge for the ONE place `diff_spans` (rolltui/Diff.hpp, untouched) crosses into
// this file's own converted surface: plugged in as a `RolltuiMdRenderOptions::highlight`,
// it must be a raw function pointer, and `diff_spans` is a fixed C++ function of a
// different shape (`std::function`-compatible, returning `std::vector<HighlightSpan>` by
// value). This trampoline calls it exactly as `ro.highlight = diff_spans;` did and forwards
// its answer through the sink — the same shape `Markdown.cpp`'s own `call_highlighter`
// trampoline used for an arbitrary registered `Highlighter`, specialised to one fixed
// callee. `diff_spans` itself is not converted; every OTHER call to it in this file (the
// direct calls below) is untouched.
void diff_highlight_fn(void* /*ctx*/, const char* lang, std::size_t lang_n, const RolltuiMdCodeLine* lines,
                       std::size_t line_count, std::size_t index, RolltuiMdSpanSink emit, void* sink) {
  std::vector<std::string_view> block(line_count);
  for (std::size_t i = 0; i < line_count; ++i) block[i] = std::string_view(lines[i].p, lines[i].n);
  for (const auto& s : diff_spans(std::string_view(lang, lang_n), block, index))
    emit(sink, s.begin, s.end, static_cast<unsigned char>(s.role));
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
                    RolltuiMdRenderOptions{.width = 80});
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
    auto v = render("An autolink https://example.com/p here.\n", RolltuiMdRenderOptions{.width = 80});
    check(lines_of(v)[0] == "An autolink https://example.com/p here.", "autolink URL is not repeated");
  }
  {
    auto v = render("**bold across a wrap point that is long** enough", RolltuiMdRenderOptions{.width = 20});
    check(v.size() >= 2 && has_role(v, Role::md_strong, "bold") && has_role(v, Role::md_strong, "long"),
          "inline style survives a wrap: strong on both lines");
  }
  {
    auto v = render("```cpp\nint x = 1;\n```\n", RolltuiMdRenderOptions{.width = 30});
    auto L = lines_of(v);
    check(L.size() == 3, "fenced code: top rule, one line, bottom rule (" + std::to_string(L.size()) + ")");
    check(L[0].rfind("\xE2\x94\x8C cpp ", 0) == 0 && L[0].size() > 10, "code box top carries the info string: [" + L[0] + "]");
    check(L[1] == "\xE2\x94\x82 int x = 1;                 \xE2\x94\x82", "code line is padded to the box: [" + L[1] + "]");
    check(has_role(v, Role::md_code_block, "int x = 1;") && has_role(v, Role::md_code_label, "cpp"), "code roles");
    check(v[0].width == 30 && v[1].width == 30 && v[2].width == 30, "code box is exactly the width");
  }
  {
    auto v = render("Text before\n\n```python\ndef f():\n    return 1\n", RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 6 && L[2].rfind("\xE2\x94\x8C python", 0) == 0 && L[5].rfind("\xE2\x94\x94", 0) == 0,
          "an unterminated fence renders as a code block to the end");
    check(has_role(v, Role::md_code_block, "return 1"), "its last line is code");
  }
  // ---- highlighter seam (plan/phase-12.md m2) -------------------------------------
  {
    int calls = 0;
    RolltuiMdRenderOptions ro{.width = 30};
    ro.highlight = toy_highlight_fn;
    ro.highlight_ctx = &calls;
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
    RolltuiMdRenderOptions ro{.width = 40};
    ro.highlight = toy_highlight_fn;
    ro.highlight_ctx = &calls;
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
    RolltuiMdRenderOptions ro{.width = 30};
    ro.highlight = toy_highlight_fn;
    ro.highlight_ctx = &calls;
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
    RolltuiMdRenderOptions ro{.width = 30};  // .highlight left at its NULL default: unset
    Lines r = render("```cpp\nint x = 1;\n```\n", ro);
    check(r.highlight_clean(), "an unregistered highlighter leaves the report clean");
    check(has_role(r.lines(), Role::md_code_block, "int x = 1;"),
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
    RolltuiMdRenderOptions ro{.width = 30};
    ro.highlight = malformed_highlight_fn;
    Lines r = render("```cpp\nint x = 1;\n```\n", ro);  // the line is "int x = 1;", 10 bytes
    check(!r.highlight_clean(), "malformed spans are reported, not dropped silently");
    check(r.clamped_count() == 3,
          "each malformed span gets its own named entry (" + std::to_string(r.clamped_count()) + ")");
    std::string all;
    for (std::size_t i = 0; i < r.clamped_count(); ++i) all.append(r.clamped(i)).append("\n");
    check(all.find("runs backwards") != std::string::npos, "the backwards span is named: [" + all + "]");
    check(all.find("overlaps an earlier span") != std::string::npos, "the overlapping span is named: [" + all + "]");
    check(all.find("exceeds the line") != std::string::npos, "the past-the-line span is named: [" + all + "]");
    check(has_role(r.lines(), Role::accent_1, "int"),
          "the one well-formed span still renders despite its malformed neighbours");
  }
  {
    auto v = render("<div>\n<b>raw</b>\n</div>\n", RolltuiMdRenderOptions{.width = 30});
    check(has_role(v, Role::md_code_block, "<b>raw</b>") && has_role(v, Role::md_code_label, "html"),
          "an HTML block is shown as code, never interpreted");
  }
  {
    auto v = render("- one\n- two\n  - nested\n- three\n", RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 4 && L[0] == "\xE2\x80\xA2 one" && L[2] == "  \xE2\x80\xA2 nested" && L[3] == "\xE2\x80\xA2 three",
          "tight bullet list with a nested list: " + L[0] + " | " + L[2]);
    check(has_role(v, Role::md_list_marker, "\xE2\x80\xA2"), "bullet in md_list_marker");
  }
  {
    auto v = render("1. a\n2. b\n3. c\n", RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 3 && L[0] == "1. a" && L[2] == "3. c", "ordered list numbering");
    auto w = render("9. a\n10. b\n", RolltuiMdRenderOptions{.width = 40});
    auto M = lines_of(w);
    check(M.size() == 2 && M[0] == " 9. a" && M[1] == "10. b", "ordered markers align on the widest number: [" + M[0] + "]");
  }
  {
    auto v = render("- [ ] todo\n- [x] done\n", RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 2 && L[0] == "[ ] todo" && L[1] == "[x] done", "task list markers");
  }
  {
    auto v = render("- a long item that wraps onto a second line for sure\n", RolltuiMdRenderOptions{.width = 24});
    auto L = lines_of(v);
    check(L.size() >= 2 && L[0].rfind("\xE2\x80\xA2 ", 0) == 0 && L[1].rfind("  ", 0) == 0 && L[1][2] != ' ',
          "hanging indent under a bullet: [" + L[1] + "]");
  }
  {
    auto v = render("1. first\n\n   para two\n2. second\n", RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 5 && L[0] == "1. first" && L[1].empty() && L[2] == "   para two" && L[3].empty() && L[4] == "2. second",
          "loose list: blank lines between blocks and items");
  }
  {
    auto v = render("> quoted *text*\n> more\n", RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 1 && L[0] == "\xE2\x94\x82 quoted text more", "blockquote bar and joined soft break: [" + L[0] + "]");
    check(has_role(v, Role::md_quote, "quoted") && has_role(v, Role::md_emphasis, "text"), "quote base role, emphasis inside");
    auto n = render("> outer\n>\n> > inner\n", RolltuiMdRenderOptions{.width = 40});
    auto N = lines_of(n);
    check(N.size() == 3 && N[1] == "\xE2\x94\x82" && N[2] == "\xE2\x94\x82 \xE2\x94\x82 inner", "nested quotes: [" + N[2] + "]");
  }
  {
    auto v = render("---\n", RolltuiMdRenderOptions{.width = 10});
    auto L = lines_of(v);
    check(L.size() == 1 && v[0].width == 10 && has_role(v, Role::md_rule, "\xE2\x94\x80"), "thematic break spans the width");
  }
  {
    auto v = render("| a | b |\n|---|--:|\n| longer cell | 1 |\n", RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 5, "table: top, head, separator, body, bottom (" + std::to_string(L.size()) + ")");
    check(L.size() == 5 && L[1] == "\xE2\x94\x82 a           \xE2\x94\x82 b \xE2\x94\x82", "header row: [" + L[1] + "]");
    check(L.size() == 5 && L[3] == "\xE2\x94\x82 longer cell \xE2\x94\x82 1 \xE2\x94\x82", "right-aligned cell: [" + L[3] + "]");
    check(has_role(v, Role::md_table_header, "a") && has_role(v, Role::md_table_border, "\xE2\x94\x82"), "table roles");
  }
  {
    auto v = render("| alpha beta gamma | delta epsilon |\n|---|---|\n| one two three four | five |\n", RolltuiMdRenderOptions{.width = 24});
    auto L = lines_of(v);
    bool fits = true;
    for (const RolltuiMdLine& l : v) fits &= (l.width <= 24);
    check(fits && L.size() > 5, "a wide table shrinks by wrapping cells (" + std::to_string(L.size()) + " lines)");
    check(never_drops("| alpha beta gamma | delta epsilon |\n|---|---|\n| one two three four | five |\n", 24).empty(),
          "…and keeps every character");
  }
  {
    std::string src = "| a | b | c | d | e |\n|---|---|---|---|---|\n| 1 | 2 | 3 | 4 | 5 |\n";
    auto v = render(src, RolltuiMdRenderOptions{.width = 12});
    check(has_role(v, Role::md_code_label, "table") && has_role(v, Role::md_code_block, "| a |") &&
              has_role(v, Role::md_code_block, "---|"),
          "a table that cannot fit at one cell per column is shown as its source in a code block");
    check(never_drops(src, 12).empty(), "…and keeps every character");
  }
  {
    auto v = render("line one  \nline two\n", RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L.size() == 2 && L[0] == "line one" && L[1] == "line two", "hard break splits the paragraph");
  }
  {
    std::string src = std::string("&amp; &lt;x&gt; &#65;&#x42; &copy; &bogus; a") + '\0' + "b";
    auto v = render(src, RolltuiMdRenderOptions{.width = 40});
    auto L = lines_of(v);
    check(L[0].find("& <x> AB \xC2\xA9 &bogus; a\xEF\xBF\xBD" "b") != std::string::npos, "entities decoded, unknown kept, NUL → U+FFFD: [" + L[0] + "]");
  }
  {
    auto v = render("![alt text](https://x.y/i.png)", RolltuiMdRenderOptions{.width = 40});
    check(lines_of(v)[0] == "alt text (https://x.y/i.png)", "image renders as alt text plus URL");
  }
  {
    auto v = render("", RolltuiMdRenderOptions{.width = 40});
    check(v.empty(), "empty source renders no lines");
    auto w = render("\n\n\n", RolltuiMdRenderOptions{.width = 40});
    check(w.empty(), "blank source renders no lines");
  }
  {
    // The block TREE left the public header in Phase 15 m4 (each implementation shapes it
    // the way its language wants); what a caller can ask is what the top-level blocks ARE,
    // which is what this assertion was always really checking.
    const std::string src = "# H\n\n- a\n\n```\nc\n```\n";
    RolltuiMdDoc* d = rolltui_md_doc_new();
    rolltui_md_parse(d, src.data(), src.size());
    std::size_t code_n = 0;
    const char* code_p = rolltui_md_doc_block_code(d, 2, &code_n);
    check(rolltui_md_doc_block_count(d) == 3 && rolltui_md_doc_block_kind(d, 0) == ROLLTUI_MD_BLOCK_HEADING &&
              rolltui_md_doc_block_kind(d, 1) == ROLLTUI_MD_BLOCK_LIST &&
              rolltui_md_doc_block_kind(d, 2) == ROLLTUI_MD_BLOCK_CODE && std::string_view(code_p, code_n) == "c\n",
          "block tree shape");
    rolltui_md_doc_free(d);
  }
  // ---- long code blocks: fold and cap (plan/phase-12.md m5b) ------------------------
  // THE BYTE-IDENTICAL CONTROL for this feature is the 100-odd assertions above: every
  // one of them renders with RolltuiMdRenderOptions{} defaults — no thresholds — and every
  // one still passes. What is asserted here is that the OFF state is off by construction,
  // not by the numbers happening to line up.
  {
    const std::string doc = "before\n\n```cpp\na\nb\nc\nd\ne\n```\n\nafter\n";
    auto text_of = [](const Lines& r) { return plain_text(r.lines()); };
    {
      const Lines r = render(doc, RolltuiMdRenderOptions{.width = 40});
      check(r.code_blocks().size() == 1 && !r.code_blocks()[0].foldable && !r.code_blocks()[0].folded &&
                r.code_blocks()[0].hidden == 0 && r.code_blocks()[0].header_line == ROLLTUI_MD_NO_LINE &&
                r.code_blocks()[0].marker_line == ROLLTUI_MD_NO_LINE,
            "no thresholds: the block is reported but nothing folds, caps or gains a row");
      check(r.code_blocks()[0].lines == 5 && r.code_blocks()[0].bytes == 10 && r.code_blocks()[0].lang() == "cpp",
            "…and it is reported with its language, line count and byte size");
      check(r.text().substr(r.code_blocks()[0].text_begin, r.code_blocks()[0].text_end - r.code_blocks()[0].text_begin) ==
                "a\nb\nc\nd\ne\n",
            "text_begin/text_end bracket exactly the block's own lines in the logical text");
    }
    {
      RolltuiMdRenderOptions ro{.width = 40};
      ro.fold_over_lines = 3;
      const Lines r = render(doc, ro);
      const std::string drawn = text_of(r);
      check(r.code_blocks().size() == 1 && r.code_blocks()[0].folded && r.code_blocks()[0].foldable,
            "a block over the threshold arrives FOLDED");
      check(drawn.find("\xE2\x96\xB8 cpp \xC2\xB7 5 lines \xC2\xB7 10 B") != std::string::npos,
            "…as one header row naming its language, line count and size [" + drawn + "]");
      check(drawn.find("\nb\n") == std::string::npos && drawn.find("\xE2\x94\x8C") == std::string::npos,
            "…with no body and no box");
      // THE RULE THE WHOLE DESIGN RESTS ON (rolltui_markdown.h): a fold hides LINES, never
      // TEXT. If this ever fails, every offset after a folded block moves when it opens
      // and a find highlight lands on the wrong bytes.
      check(r.text().find("a\nb\nc\nd\ne") != std::string_view::npos,
            "…and EVERY BYTE of the block is still in the logical text");
      check(r.code_blocks()[0].header_line != ROLLTUI_MD_NO_LINE &&
                r.lines()[r.code_blocks()[0].header_line].spans().size() >= 2,
            "the header row is reported by line number, which is what a click routes by");
    }
    {
      RolltuiMdRenderOptions ro{.width = 40};
      ro.fold_over_lines = 10;
      const Lines r = render(doc, ro);
      check(!r.code_blocks()[0].foldable && text_of(r) == text_of(render(doc, RolltuiMdRenderOptions{.width = 40})),
            "a block UNDER the threshold renders exactly as it does with no threshold at all");
    }
    {
      RolltuiMdRenderOptions ro{.width = 40};
      ro.cap_lines = 3;
      const Lines r = render(doc, ro);
      const std::string drawn = text_of(r);
      check(!r.code_blocks()[0].folded && r.code_blocks()[0].hidden == 2 && r.code_blocks()[0].marker_line != ROLLTUI_MD_NO_LINE,
            "an unfolded block over the CAP draws its first lines and marks the rest");
      check(drawn.find("\xE2\x96\xBC 2 more") != std::string::npos && drawn.find("\nd\n") == std::string::npos,
            "…with the same '▼ N more' marker the transcript uses [" + drawn + "]");
      check(r.text().find("a\nb\nc\nd\ne") != std::string_view::npos, "…and the capped lines are still in the text");
    }
    {
      // The two overrides, in both directions, which is what a user's toggle is.
      RolltuiMdRenderOptions ro{.width = 40};
      ro.fold_over_lines = 3;
      ro.cap_lines = 3;
      std::vector<RolltuiMdFoldState> states_open{{0, /*folded=*/0, /*uncapped=*/0}};
      ro.states = states_open.data();
      ro.state_count = states_open.size();
      const Lines open = render(doc, ro);
      check(!open.code_blocks()[0].folded && open.code_blocks()[0].foldable && open.code_blocks()[0].hidden == 2 &&
                plain_text(open.lines()).find("\xE2\x96\xBE cpp") != std::string::npos,
            "opening an over-threshold block shows it with a ▾ header, still CAPPED");
      std::vector<RolltuiMdFoldState> states_full{{0, /*folded=*/0, /*uncapped=*/1}};
      ro.states = states_full.data();
      ro.state_count = states_full.size();
      const Lines full = render(doc, ro);
      const std::string full_drawn = plain_text(full.lines());
      check(full.code_blocks()[0].hidden == 0 && full.code_blocks()[0].marker_line == ROLLTUI_MD_NO_LINE &&
                full_drawn.find("d ") != std::string::npos && full_drawn.find("e ") != std::string::npos,
            "…and lifting the cap on it shows every line, with no marker row left [" + full_drawn + "]");
      ro.fold_over_lines = 0;
      ro.cap_lines = 0;
      std::vector<RolltuiMdFoldState> states_shut{{0, /*folded=*/1, /*uncapped=*/0}};
      ro.states = states_shut.data();
      ro.state_count = states_shut.size();
      const Lines shut = render(doc, ro);
      check(shut.code_blocks()[0].folded && shut.code_blocks()[0].foldable,
            "a block the threshold would NOT fold still folds when a state says so — and keeps its header");
    }
    {
      // A block's index is a DOCUMENT fact: a table that falls back to code because it
      // is too narrow must not take a number, or a fold toggle would move to another
      // block when the window is resized.
      const std::string with_table = "| a | b |\n|---|---|\n| 1 | 2 |\n\n```\nx\n```\n";
      RolltuiMdRenderOptions ro{.width = 8};
      ro.fold_over_lines = 1;
      const Lines narrow = render(with_table, ro);
      check(narrow.code_blocks().size() == 1 && narrow.code_blocks()[0].index == 0,
            "the too-narrow table's fallback code box is NOT numbered; the real block keeps index 0");
      ro.width = 40;
      const Lines wide = render(with_table, ro);
      check(wide.code_blocks().size() == 1 && wide.code_blocks()[0].index == 0,
            "…so the same block has the same index at a width where the table fits");
    }
    {
      // Nested and numbered in document order, from the same counter as a top-level one.
      RolltuiMdRenderOptions ro{.width = 40};
      ro.fold_over_lines = 1;
      const Lines r = render("```\ntop\nalso\n```\n\n- item\n\n  ```\n  in\n  list\n  ```\n", ro);
      check(r.code_blocks().size() == 2 && r.code_blocks()[0].index == 0 && r.code_blocks()[1].index == 1,
            "a code block inside a list item numbers from the same counter as a top-level one");
      check(r.code_blocks()[1].header_line > r.code_blocks()[0].header_line,
            "…and its header row is below, so the line numbers are the document's, not each block's");
    }
    {
      // The standing degenerate-size rule: 1 and 0 cells, with folding on.
      for (int w : {0, 1, 2, 5}) {
        RolltuiMdRenderOptions ro{.width = w};
        ro.fold_over_lines = 2;
        ro.cap_lines = 2;
        const Lines r = render(doc, ro);
        check(!r.code_blocks().empty(), "folding survives width " + std::to_string(w));
        std::vector<RolltuiMdFoldState> states{{0, 0, 0}};
        ro.states = states.data();
        ro.state_count = states.size();
        const Lines open = render(doc, ro);
        check(!open.lines().empty(), "…and so does capping at width " + std::to_string(w));
      }
    }
    {
      // An HTML block is opaque code and folds like one; it is never highlighted.
      RolltuiMdRenderOptions ro{.width = 40};
      ro.fold_over_lines = 2;
      const Lines r = render("<div>\n<p>a</p>\n<p>b</p>\n</div>\n", ro);
      check(r.code_blocks().size() == 1 && r.code_blocks()[0].folded && r.code_blocks()[0].lang() == "html",
            "an HTML block folds too, and names itself html");
    }
    {
      check(summary("", 1, 5) == "code \xC2\xB7 1 line \xC2\xB7 5 B",
            "a bare fence has no language to name, so the summary calls it code, and 1 line is singular");
      check(summary("diff", 42, 1229).find("1.2 kB") != std::string::npos,
            "…and a size over 1024 reads in kB [" + summary("diff", 42, 1229) + "]");
    }
  }
  // ---- the diff colouriser (plan/phase-12.md m5, word level in m5b) ----------------
  // It rides m2's seam rather than being a second mechanism — which is the cheapest rung
  // and also the test of whether that seam was placed right. `diff_spans` is Diff.hpp's
  // own, untouched function; only its direct calls appear below (see the scope note at the
  // top of this file) — the ONE place it is plugged into the renderer's `highlight` field
  // is bridged by `diff_highlight_fn`, in the section after this one.
  {
    // A single line, with no neighbours: the line level, unchanged from m5.
    auto one = [](std::string_view lang, std::string_view line) {
      const std::vector<std::string_view> block{line};
      return diff_spans(lang, block, 0);
    };
    auto role_of = [&](std::string_view lang, std::string_view line) {
      const auto sp = one(lang, line);
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
    check(one("", "+added").empty() && one("python", "-x = 1").empty(),
          "a bare fence and another language get NO spans, whatever the content looks like");
    check(one("patch", "+x").size() == 1 && one("udiff", "+x").size() == 1,
          "'patch' and 'udiff' are the same claim as 'diff'");
    // A span covers the WHOLE line, marker included: a half-coloured line reads as a bug,
    // and the marker is doing separate work as the non-colour signal.
    const auto sp = one("diff", "+abc");
    check(sp.size() == 1 && sp[0].begin == 0 && sp[0].end == 4, "the span covers the whole line, marker included");
    check(diff_spans("diff", std::vector<std::string_view>{}, 0).empty(), "an index past the block asks for nothing");

    // ---- word level (m5b): the PAIRING rule, then the REFINEMENT rule --------------
    // Every case here is stated in Diff.hpp; the point of the table is that the rules
    // are asserted rather than tuned until a screenshot looked right.
    auto spans_of = [](std::vector<std::string_view> block, std::size_t i) { return diff_spans("diff", block, i); };
    auto word_range_of = [&](std::vector<std::string_view> block, std::size_t i, std::size_t& b, std::size_t& e) {
      for (const auto& x : spans_of(std::move(block), i))
        if (x.role == Role::diff_added_word || x.role == Role::diff_removed_word) { b = x.begin; e = x.end; return true; }
      return false;
    };
    {
      const std::vector<std::string_view> pair{"-int foo = 1;", "+int bar = 1;"};
      std::size_t b = 0, e = 0;
      check(word_range_of(pair, 0, b, e) && pair[0].substr(b, e - b) == "foo",
            "the removed side's changed word is marked, and it is exactly the word");
      check(word_range_of(pair, 1, b, e) && pair[1].substr(b, e - b) == "bar",
            "…and so is the added side's: BOTH halves of a pair, never only the '+'");
      const auto three = spans_of(pair, 0);
      check(three.size() == 3 && three[0].begin == 0 && three[0].role == Role::diff_removed &&
                three[1].role == Role::diff_removed_word && three[2].end == pair[0].size() &&
                three[2].role == Role::diff_removed,
            "three NON-OVERLAPPING spans, line/word/line, so the renderer never has to clamp");
    }
    {
      std::size_t b = 0, e = 0;
      check(!word_range_of({"-wholly different", "+nothing alike here"}, 0, b, e),
            "nothing common at either end: no word span, because the line role already said it all");
      check(!word_range_of({"-a b c", "+a b c d", "+extra"}, 0, b, e),
            "2 additions against 1 removal is NOT a pair: unequal runs get the line level only");
      check(!word_range_of({"-a b c", " context", "+a b d"}, 0, b, e),
            "a context line between them breaks the run: the addition must IMMEDIATELY follow");
      check(word_range_of({"-a b c", "+a b c d"}, 1, b, e), "a pure insertion marks the added side…");
      check(!word_range_of({"-a b c", "+a b c d"}, 0, b, e),
            "…and NOT the removed side, whose middle is empty — nothing of its own changed");
    }
    {
      // Two removals then two additions: paired by INDEX, i with i. The second pair's
      // change is what proves it is not always comparing against the first line.
      const std::vector<std::string_view> block{"-one alpha end", "-two beta end", "+one gamma end", "+two delta end"};
      std::size_t b = 0, e = 0;
      check(word_range_of(block, 0, b, e) && block[0].substr(b, e - b) == "alpha",
            "a run of 2 against 2 pairs line 0 with line 2");
      check(word_range_of(block, 1, b, e) && block[1].substr(b, e - b) == "beta",
            "…and line 1 with line 3, by index");
      check(word_range_of(block, 3, b, e) && block[3].substr(b, e - b) == "delta",
            "…which is symmetric: the last addition pairs back to the last removal");
    }

    // Through the renderer, on the cells: the roles land, and the +/- prefixes SURVIVE —
    // which is what makes a diff readable with colour switched off (the Done-when).
    // `diff_highlight_fn` (above) bridges `diff_spans` into the raw `highlight` field.
    RolltuiMdRenderOptions ro{};
    ro.width = 40;
    ro.highlight = diff_highlight_fn;
    const Lines r = render("```diff\n@@ -1,2 +1,2 @@\n-old line\n+new line\n context\n```\n", ro);
    check(r.highlight_clean(), "the colouriser never produces a span the renderer has to clamp");
    bool has_added = false, has_removed = false, has_hunk = false, marks_kept = false;
    bool has_added_word = false, has_removed_word = false;
    std::string all, added_word, removed_word;
    for (const RolltuiMdLine& l : r.lines())
      for (const RolltuiMdSpan& x : l.spans()) {
        const Role role = static_cast<Role>(x.role);
        all.append(x.text());
        if (role == Role::diff_added) has_added = true;
        if (role == Role::diff_removed) has_removed = true;
        if (role == Role::accent_1) has_hunk = true;
        if (role == Role::diff_added_word) { has_added_word = true; added_word.append(x.text()); }
        if (role == Role::diff_removed_word) { has_removed_word = true; removed_word.append(x.text()); }
      }
    marks_kept = all.find("-old line") != std::string::npos && all.find("+new line") != std::string::npos;
    check(has_added && has_removed && has_hunk, "a ```diff fence colours through the diff roles");
    check(marks_kept, "…and the +/- prefixes are NEVER stripped: the signal a mono or CVD reader still has");
    check(has_added_word && has_removed_word && removed_word == "old" && added_word == "new",
          "the word roles reach the CELLS, on both sides [-" + removed_word + " +" + added_word + "]");
    // The control for the control: the SAME content under a bare fence has no diff role
    // anywhere on any cell. This is the milestone's Done-when, at the renderer.
    const Lines plain = render("```\n@@ -1,2 +1,2 @@\n-old line\n+new line\n context\n```\n", ro);
    bool any_diff_role = false;
    for (const RolltuiMdLine& l : plain.lines())
      for (const RolltuiMdSpan& x : l.spans())
        if (static_cast<Role>(x.role) != Role::md_code_block && static_cast<Role>(x.role) != Role::md_code_label)
          any_diff_role = true;
    check(!any_diff_role, "a bare fence over content that looks EXACTLY like a diff renders plain");
  }

  return report("rolltui markdown_test");
}
