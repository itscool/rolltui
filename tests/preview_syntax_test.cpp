// rolltui/tests/preview_syntax_test.cpp — SOURCE COLOUR IN THE PREVIEW: a file in a language the library knows is drawn with its
// keywords, strings and comments in the theme's colours.
//
// The one thing that must always hold is that COLOUR NEVER CHANGES WHAT IS DRAWN. Every sample of every shipped language is
// drawn with the languages lent and without, at several widths, and the two frames are the same characters in the same cells;
// only the styles differ. Around that: that the colours are the THEME'S (a class draws in the role `rolltui_syntax_role` names,
// and a different theme gives different cells), that a tab, a wide letter, a control character, a CRLF, a line cut at an
// ellipsis and a file with no final newline leave the colours where the code is, that scrolling and re-reading keep them on the
// right lines, that a file is coloured only when it is in a language (and the head says which), that a Markdown document's
// fenced code is coloured and stays on the block's ground, and that the picker's `no_syntax` turns all of it off and on.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_preview.h"        /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui/c/rolltui_screen.h"         /* INTERNAL: a frame of its own to draw into */
#include "rolltui/c/rolltui_syntax.h"         /* INTERNAL */
#include "rolltui/c/rolltui_widget_picker.h"  /* INTERNAL */
#include "rolltui_test.hpp"

#ifndef ROLLTUI_PRESETS_DIR
#error "ROLLTUI_PRESETS_DIR must point at rolltui/presets/themes"
#endif

namespace {
namespace fs = std::filesystem;
using testkit::check;

void write_file(const fs::path& p, const std::string& t) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << t;
}
std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
std::size_t live_bytes() {
  std::size_t live = 0;
  rolltui_mem_stats(nullptr, nullptr, nullptr, &live, nullptr, nullptr);
  return live;
}

bool same(const RolltuiStyleColor& a, const RolltuiStyleColor& b) { return a.kind == b.kind && a.index == b.index && a.r == b.r && a.g == b.g && a.b == b.b; }

// a theme, and a look at what a frame drew
struct Theme {
  RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
  explicit Theme(const char* name = "default-dark") {
    RolltuiEffectMap* fx = rolltui_theme_builtin_fill(name, std::strlen(name), styles, ROLLTUI_ROLE_COUNT);
    rolltui_effect_map_free(fx);
  }
  RolltuiStyleColor fg(unsigned char role) const { return styles[role].fg; }
  RolltuiStyleColor bg(unsigned char role) const { return styles[role].bg; }
  // the colour a class is drawn in
  RolltuiStyleColor of(unsigned char cls) const { return fg(rolltui_syntax_role(cls, ROLLTUI_ROLE_TEXT)); }
};

struct Shot {
  RolltuiFrame* f;
  int w, h;
  Shot(RolltuiPreview* pv, const Theme& th, int width, int height, int aw = 0) : f(rolltui_frame_new(width, height, th.styles[ROLLTUI_ROLE_BACKGROUND])), w(width), h(height) {
    rolltui_preview_draw(pv, f, RolltuiRect{0, 0, width, height}, th.styles, aw, 0);
  }
  ~Shot() { rolltui_frame_free(f); }
  Shot(const Shot&) = delete;
  Shot& operator=(const Shot&) = delete;
  std::string glyph(int x, int y) const {
    std::size_t n = 0;
    const char* g = rolltui_frame_glyph(f, x, y, &n);
    return std::string(g, n);
  }
  RolltuiStyle style(int x, int y) const {
    RolltuiCell c;
    rolltui_frame_cell(f, x, y, &c);
    return c.style;
  }
  std::string row(int y) const {
    std::string s;
    for (int x = 0; x < w; ++x) s += glyph(x, y);
    return s;
  }
  std::string text() const {
    std::string s;
    for (int y = 0; y < h; ++y) s += row(y) + "\n";
    return s;
  }
  // the CELL COLUMN where `needle` begins on row `y` (a glyph of more than one byte is still one column; the right half of a
  // wide one is skipped), or -1
  int find(int y, const std::string& needle) const {
    std::string s;
    std::vector<int> start;
    for (int x = 0; x < w; ++x) {
      const std::string g = glyph(x, y);
      start.push_back(g.empty() ? -1 : static_cast<int>(s.size()));
      s += g;
    }
    const std::size_t at = s.find(needle);
    if (at == std::string::npos) return -1;
    for (int x = 0; x < w; ++x)
      if (start[x] == static_cast<int>(at)) return x;
    return -1;
  }
  // the colour of the text `needle` on row `y`, if every cell of it is one colour; else a colour that is nothing
  bool colour_of(int y, const std::string& needle, RolltuiStyleColor* out) const {
    const int at = find(y, needle);
    if (at < 0) return false;
    *out = style(at, y).fg;
    for (std::size_t i = 1; i < needle.size(); ++i)
      if (!same(style(at + static_cast<int>(i), y).fg, *out)) return false;
    return true;
  }
  bool is(int y, const std::string& needle, const RolltuiStyleColor& want) const {
    RolltuiStyleColor got;
    return colour_of(y, needle, &got) && same(got, want);
  }
};

// how many body cells are drawn in a colour other than plain text's
int coloured_cells(const Shot& s, const Theme& th) {
  int n = 0;
  for (int y = 1; y < s.h; ++y)
    for (int x = 1; x < s.w - 1; ++x)
      if (s.glyph(x, y) != " " && !same(s.style(x, y).fg, th.fg(ROLLTUI_ROLE_TEXT))) ++n;
  return n;
}

struct Fixture { const char* sample; const char* path; };
const Fixture kFixtures[] = {
    {"c", "main.c"},          {"cpp", "widget.hpp"},        {"java", "Widget.java"},   {"csharp", "Thing.cs"},   {"javascript", "app.mjs"},   {"typescript", "app.ts"},
    {"python", "app.py"},     {"rust", "main.rs"},          {"go", "main.go"},         {"swift", "Thing.swift"}, {"lua", "mod.lua"},          {"luau", "mod.luau"},
    {"shell", "build.sh"},    {"batch", "build.bat"},       {"powershell", "Get-Thing.ps1"}, {"cmake", "CMakeLists.txt"}, {"json", "package.json"},  {"yaml", "ci.yml"},
    {"ini", "config.ini"},    {"toml", "Cargo.toml"},       {"xml", "pom.xml"},        {"css", "site.css"},      {"html", "index.html"},      {"ignore", ".gitignore"},
    {"gitattributes", ".gitattributes"},
};

// a preview of `path` (already written), with or without the languages lent
struct Pv {
  RolltuiPreview* pv;
  Pv(RolltuiSyntax* syn) : pv(rolltui_preview_new()) { rolltui_preview_set_syntax(pv, syn); }
  ~Pv() { rolltui_preview_free(pv); }
  Pv(const Pv&) = delete;
  Pv& operator=(const Pv&) = delete;
  void show(const fs::path& p) { rolltui_preview_set_path(pv, p.c_str(), std::strlen(p.c_str())); }
};

}  // namespace

int main() {
  const std::size_t base = live_bytes();
  const std::string tmp = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
  const fs::path root = fs::path(tmp) / ("rolltui_preview_syntax_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  const fs::path fixtures = fs::path(ROLLTUI_FIXTURE_DIR) / "syntax";
  {
    RolltuiSyntax* syn = rolltui_syntax_new_standard(nullptr);
    const Theme th;

    // ---- A C FILE, COLOURED AS THE THEME SAYS ------------------------------------------------------------------------------------
    write_file(root / "main.c", "int main(void) {\n  // note\n  return 42; /* x */\n}\n");
    {
      Pv on(syn);
      on.show(root / "main.c");
      Shot s(on.pv, th, 60, 8);
      check(s.text().find("C \xC2\xB7 4 lines") != std::string::npos, "the head names the language: " + s.row(0));
      check(s.is(1, "int", th.of(ROLLTUI_SYN_TYPE)) && s.is(1, "main", th.of(ROLLTUI_SYN_FUNCTION)), "a type and a function draw in the theme's colours for them");
      check(s.is(2, "// note", th.of(ROLLTUI_SYN_COMMENT)), "a comment draws in the comment colour, all of it");
      check(s.is(3, "return", th.of(ROLLTUI_SYN_KEYWORD)) && s.is(3, "42", th.of(ROLLTUI_SYN_NUMBER)) && s.is(3, "/* x */", th.of(ROLLTUI_SYN_COMMENT)), "a keyword, a number and a block comment");
      check(s.is(1, "(void)", th.fg(ROLLTUI_ROLE_TEXT)) || s.is(1, "{", th.fg(ROLLTUI_ROLE_TEXT)), "what is no class draws as plain text");
      check(!same(th.of(ROLLTUI_SYN_KEYWORD), th.of(ROLLTUI_SYN_STRING)) && !same(th.of(ROLLTUI_SYN_STRING), th.of(ROLLTUI_SYN_COMMENT)) && !same(th.of(ROLLTUI_SYN_KEYWORD), th.fg(ROLLTUI_ROLE_TEXT)),
            "(the control: the theme really does draw a keyword, a string, a comment and plain text in four colours)");
      bool ground = true;
      for (int y = 1; y < 5; ++y)
        for (int x = 1; x < 59; ++x) ground = ground && same(s.style(x, y).bg, th.bg(ROLLTUI_ROLE_BACKGROUND));
      check(ground, "every coloured cell stands on the window's ground: colour changes the letters, never the background");
    }
    {
      Pv off(nullptr);
      off.show(root / "main.c");
      Shot s(off.pv, th, 60, 8);
      check(s.text().find("text \xC2\xB7 4 lines") != std::string::npos, "with no languages lent the head says text, as it always did: " + s.row(0));
      check(coloured_cells(s, th) == 0, "…and nothing is coloured");
    }

    // ---- COLOUR NEVER CHANGES WHAT IS DRAWN: every sample, both ways, at several widths ------------------------------------------
    {
      int samples = 0, differing = 0;
      std::string first_bad;
      for (const Fixture& fx : kFixtures) {
        const std::string text = slurp(fixtures / (std::string(fx.sample) + ".sample"));
        write_file(root / "each" / fx.path, text);
        Pv on(syn), off(nullptr);
        on.show(root / "each" / fx.path);
        off.show(root / "each" / fx.path);
        ++samples;
        for (int w : {14, 33, 60, 100}) {
          for (int h : {6, 20}) {
            Shot a(on.pv, th, w, h), b(off.pv, th, w, h);
            // the head names the language in the coloured one and says `text` in the other: from the body down they are the same
            std::string ta, tb;
            for (int y = 1; y < h; ++y) { ta += a.row(y) + "\n"; tb += b.row(y) + "\n"; }
            if (ta != tb) { ++differing; if (first_bad.empty()) first_bad = std::string(fx.sample) + " at " + std::to_string(w) + "x" + std::to_string(h); }
            if (w == 100 && h == 20 && coloured_cells(a, th) < 8) { ++differing; first_bad = std::string(fx.sample) + " is coloured almost nowhere"; }
          }
        }
      }
      check(samples == 25 && differing == 0, "25 samples x 8 sizes: colour never changes a character or where it is drawn [" + first_bad + "]");
    }

    // ---- A TAB, A WIDE LETTER, A CONTROL CHARACTER, A LINE THAT IS CUT --------------------------------------------------------------
    {
      write_file(root / "tabs.c", "\tif (x) {\n\t\treturn \"caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\";\n\t}\n\t// \xE6\x97\xA5 note\n\tint y = 1;\x1b" "[31m; // esc\n");
      Pv on(syn), off(nullptr);
      on.show(root / "tabs.c");
      off.show(root / "tabs.c");
      Shot a(on.pv, th, 50, 8), b(off.pv, th, 50, 8);
      std::string ta, tb;
      for (int y = 1; y < 8; ++y) { ta += a.row(y) + "\n"; tb += b.row(y) + "\n"; }
      check(ta == tb, "tabs, wide letters and a control character draw the same characters coloured or not");
      check(a.is(1, "if", th.of(ROLLTUI_SYN_KEYWORD)) && a.find(1, "if") == 5, "a tab is four columns and the keyword after it is coloured where it is [col " + std::to_string(a.find(1, "if")) + "]");
      check(a.is(2, "return", th.of(ROLLTUI_SYN_KEYWORD)) && a.find(2, "return") == 9, "…two tabs, eight columns");
      RolltuiStyleColor c;
      check(a.colour_of(2, "\"caf\xC3\xA9", &c) && same(c, th.of(ROLLTUI_SYN_STRING)), "a string with an accented letter is one colour through it");
      check(a.find(5, "\xE2\x90\x9B") > 0, "the escape character is drawn as its picture, and not sent to the terminal");
      check(a.is(4, "// ", th.of(ROLLTUI_SYN_COMMENT)), "a comment holding a wide letter is coloured");
    }
    {
      std::string longline = "int v = 1; // ";
      for (int i = 0; i < 40; ++i) longline += "word" + std::to_string(i) + " ";
      write_file(root / "long.c", longline + "\nint w;\n");
      Pv on(syn), off(nullptr);
      on.show(root / "long.c");
      off.show(root / "long.c");
      Shot a(on.pv, th, 40, 5), b(off.pv, th, 40, 5);
      check(a.row(1) == b.row(1) && a.glyph(38, 1) == "\xE2\x80\xA6", "a long line is cut at an ellipsis in the same cell coloured or not");
      check(same(a.style(38, 1).fg, th.fg(ROLLTUI_ROLE_TEXT_MUTED)), "…and the ellipsis is the dim one, not a comment's");
      check(a.is(1, "int", th.of(ROLLTUI_SYN_TYPE)) && a.is(1, "// word0", th.of(ROLLTUI_SYN_COMMENT)), "…with the code before it coloured and the comment running to the cut");
      check(a.is(2, "int", th.of(ROLLTUI_SYN_TYPE)), "…and the next line coloured as its own");
    }

    // ---- CRLF, NO FINAL NEWLINE, BLANK LINES, ONE LINE -------------------------------------------------------------------------------
    {
      const std::string lf = "int a;\n\n// c\nreturn 1;\n";
      std::string crlf;
      for (char c : lf) { if (c == '\n') crlf += '\r'; crlf += c; }
      write_file(root / "lf.c", lf);
      write_file(root / "crlf.c", crlf);
      write_file(root / "nofinal.c", lf.substr(0, lf.size() - 1));
      write_file(root / "one.c", "return 1;");
      Pv a(syn), b(syn), c(syn), d(syn);
      a.show(root / "lf.c");
      b.show(root / "crlf.c");
      c.show(root / "nofinal.c");
      d.show(root / "one.c");
      Shot sa(a.pv, th, 40, 8), sb(b.pv, th, 40, 8), sc(c.pv, th, 40, 8), sd(d.pv, th, 40, 4);
      bool crlf_same = true, nofinal_same = true;
      for (int y = 1; y < 8; ++y)
        for (int x = 1; x < 39; ++x) {
          crlf_same = crlf_same && sa.glyph(x, y) == sb.glyph(x, y) && same(sa.style(x, y).fg, sb.style(x, y).fg);
          nofinal_same = nofinal_same && sa.glyph(x, y) == sc.glyph(x, y) && same(sa.style(x, y).fg, sc.style(x, y).fg);
        }
      check(crlf_same, "a CRLF file is coloured cell for cell as the LF one is, and shows no \\r");
      check(nofinal_same, "a file with no final newline is coloured as with one");
      check(sa.is(3, "// c", th.of(ROLLTUI_SYN_COMMENT)) && sa.is(4, "return", th.of(ROLLTUI_SYN_KEYWORD)), "a blank line between two coloured ones keeps each colour on its own line");
      check(sd.is(1, "return", th.of(ROLLTUI_SYN_KEYWORD)) && sd.text().find("C \xC2\xB7 1 line ") != std::string::npos, "a file of one line is coloured, and the head says `1 line`");
    }

    // ---- SCROLLING, AND READING AGAIN --------------------------------------------------------------------------------------------------
    {
      std::string big;
      for (int i = 0; i < 120; ++i) big += (i % 3 == 0 ? "int v" : i % 3 == 1 ? "// note " : "return ") + std::to_string(i) + ";\n";
      write_file(root / "big.c", big);
      Pv pv(syn);
      pv.show(root / "big.c");
      Shot top(pv.pv, th, 40, 8);
      rolltui_preview_scroll_by(pv.pv, 50);
      Shot mid(pv.pv, th, 40, 8);
      // line 50 is `i % 3 == 2` -> "return 50;", line 51 -> "int v51;", line 52 -> "// note 52;"
      check(mid.find(1, "return 50;") >= 0 && mid.is(1, "return", th.of(ROLLTUI_SYN_KEYWORD)), "after scrolling, the line at the top is the one it should be and is coloured for what it is");
      check(mid.is(2, "int", th.of(ROLLTUI_SYN_TYPE)) && mid.is(3, "// note 52", th.of(ROLLTUI_SYN_COMMENT)), "…and so are the lines under it: the colours travel with the lines");
      rolltui_preview_scroll_edge(pv.pv, 1);
      Shot end(pv.pv, th, 40, 8);
      check(end.find(7, "return 119;") >= 0 || end.find(7, "// note 118;") >= 0 || end.find(7, "int v117;") >= 0, "at the end the last line is on the last row");
      bool lines_ok = true;
      for (int y = 1; y < 8; ++y) {
        const std::string r = end.row(y);
        const bool kw = r.find("return") == 1, ty = r.find("int v") == 1, cm = r.find("// note") == 1;
        if (kw) lines_ok = lines_ok && end.is(y, "return", th.of(ROLLTUI_SYN_KEYWORD));
        if (ty) lines_ok = lines_ok && end.is(y, "int", th.of(ROLLTUI_SYN_TYPE));
        if (cm) lines_ok = lines_ok && end.is(y, "// note", th.of(ROLLTUI_SYN_COMMENT));
      }
      check(lines_ok, "…and every row at the end has the colour its own text calls for");
    }
    {
      // the file is written again under the reader: what is drawn is coloured for what it now is
      write_file(root / "grow.c", "int a;\nint b;\n");
      timespec ts[2];
      ts[0].tv_sec = ts[1].tv_sec = ::time(nullptr) - 3600;
      ts[0].tv_nsec = ts[1].tv_nsec = 0;
      ::utimensat(AT_FDCWD, (root / "grow.c").c_str(), ts, 0);
      Pv pv(syn);
      pv.show(root / "grow.c");
      { Shot s(pv.pv, th, 40, 6); check(s.is(2, "int", th.of(ROLLTUI_SYN_TYPE)), "(before: `int b;` is a type and a name)"); }
      write_file(root / "grow.c", "int a;\n// int b;\nreturn 3;\n");
      ::utimensat(AT_FDCWD, (root / "grow.c").c_str(), ts, 0);
      check(rolltui_preview_refresh(pv.pv) == 1, "a file that was written is read again");
      Shot s(pv.pv, th, 40, 6);
      check(s.is(2, "// int b;", th.of(ROLLTUI_SYN_COMMENT)) && s.is(3, "return", th.of(ROLLTUI_SYN_KEYWORD)), "…and coloured for what it now says: a commented line, a new statement");
      write_file(root / "grow.c", "plain now\n");
      ::utimensat(AT_FDCWD, (root / "grow.c").c_str(), ts, 0);
      rolltui_preview_refresh(pv.pv);
      Shot t(pv.pv, th, 40, 6);
      check(t.row(1).find("plain now") == 1 && t.is(1, "plain now", th.fg(ROLLTUI_ROLE_TEXT)), "…and a file that says nothing a language knows is drawn as plain text within the same language");
    }

    // ---- WHEN IS A FILE COLOURED: only in a language, and the languages are lent ---------------------------------------------------------
    {
      write_file(root / "notes.txt", "int x = 1; // not code\n");
      write_file(root / "Makefile", "all:\n\tcc -o a a.c\n");
      write_file(root / "script", "#!/usr/bin/env python3\nimport os\n");
      write_file(root / "noext", "int x;\n");
      Pv pv(syn);
      pv.show(root / "notes.txt");
      { Shot s(pv.pv, th, 40, 4); check(coloured_cells(s, th) == 0 && s.row(0).find("text \xC2\xB7") != std::string::npos, "a .txt file is not coloured, and is `text`"); }
      pv.show(root / "Makefile");
      { Shot s(pv.pv, th, 40, 4); check(coloured_cells(s, th) == 0, "a Makefile (no language for it) is not coloured"); }
      pv.show(root / "noext");
      { Shot s(pv.pv, th, 40, 4); check(coloured_cells(s, th) == 0, "a file with no extension and no shebang is not coloured"); }
      pv.show(root / "script");
      { Shot s(pv.pv, th, 40, 4); check(s.row(0).find("Python") != std::string::npos && s.is(2, "import", th.of(ROLLTUI_SYN_KEYWORD)), "a file with no extension is coloured by its shebang, and named: " + s.row(0)); }
      pv.show(root / "main.c");
      { Shot s(pv.pv, th, 40, 4); check(s.is(1, "int", th.of(ROLLTUI_SYN_TYPE)), "moving from file to file: the next is coloured for itself"); }
      pv.show(root / "notes.txt");
      { Shot s(pv.pv, th, 40, 4); check(coloured_cells(s, th) == 0, "…and back to one that is not: nothing of the last one's colour is left"); }
    }
    {
      write_file(root / "toggle.c", "return 1;\n");
      Pv pv(nullptr);
      pv.show(root / "toggle.c");
      { Shot s(pv.pv, th, 30, 4); check(coloured_cells(s, th) == 0, "a preview with no languages lent draws a C file plain"); }
      rolltui_preview_set_syntax(pv.pv, syn);
      { Shot s(pv.pv, th, 30, 4); check(s.is(1, "return", th.of(ROLLTUI_SYN_KEYWORD)), "lending them colours the file already shown"); }
      rolltui_preview_set_syntax(pv.pv, nullptr);
      { Shot s(pv.pv, th, 30, 4); check(coloured_cells(s, th) == 0, "taking them back draws it plain again"); }
      rolltui_preview_set_syntax(pv.pv, syn);
      rolltui_preview_set_syntax(pv.pv, syn);
      { Shot s(pv.pv, th, 30, 4); check(s.is(1, "return", th.of(ROLLTUI_SYN_KEYWORD)), "lending the same set twice changes nothing"); }
    }

    // ---- WHAT IS NOT TEXT, AND WHAT IS TOO BIG ------------------------------------------------------------------------------------------
    {
      std::string bin = "int x;\n";
      bin += std::string(4, '\0');
      bin += "more\n";
      write_file(root / "bin.c", bin);
      Pv pv(syn);
      pv.show(root / "bin.c");
      check(rolltui_preview_kind(pv.pv) == ROLLTUI_PREVIEW_HEX, "a `.c` file with a NUL in it is bytes, not code: the hex view is untouched");
      std::string huge;
      while (huge.size() < 300 * 1024) huge += "int counter = 12345; // a trailing comment that makes the line a little longer\n";
      write_file(root / "huge.c", huge);
      pv.show(root / "huge.c");
      Shot s(pv.pv, th, 60, 6);
      check(rolltui_preview_kind(pv.pv) == ROLLTUI_PREVIEW_TEXT && s.row(0).find("C \xC2\xB7 first") != std::string::npos && s.is(1, "int", th.of(ROLLTUI_SYN_TYPE)),
            "a file past the read limit is coloured as far as it is shown, and the head still says how much: " + s.row(0));
    }

    // ---- THE THEME IS THE THEME'S ------------------------------------------------------------------------------------------------------------
    {
      const Theme light("default-light");
      Pv pv(syn);
      pv.show(root / "main.c");
      Shot dark(pv.pv, th, 40, 5), lit(pv.pv, light, 40, 5);
      check(dark.is(3, "return", th.of(ROLLTUI_SYN_KEYWORD)) && lit.is(3, "return", light.of(ROLLTUI_SYN_KEYWORD)), "each theme draws a keyword in its own colour for it");
      check(!same(th.of(ROLLTUI_SYN_KEYWORD), light.of(ROLLTUI_SYN_KEYWORD)) || !same(th.of(ROLLTUI_SYN_STRING), light.of(ROLLTUI_SYN_STRING)), "(the control: the two themes are not the same colours)");
    }

    // ---- A MARKDOWN DOCUMENT'S FENCED CODE ---------------------------------------------------------------------------------------------------
    {
      write_file(root / "doc.md", "# Notes\n\nSome `code` here.\n\n```python\ndef f(x):\n    return x * 2  # double\n```\n\n```nosuchlang\nreturn 1\n```\n\n```\nreturn 2\n```\n");
      Pv on(syn), off(nullptr);
      on.show(root / "doc.md");
      off.show(root / "doc.md");
      Shot a(on.pv, th, 50, 24), b(off.pv, th, 50, 24);
      check(a.text() == b.text(), "a Markdown document draws the same characters with its code coloured or not");
      int y_def = -1, y_ret = -1, y_nosuch = -1, y_bare = -1;
      for (int y = 0; y < 24; ++y) {
        const std::string r = a.row(y);
        if (y_def < 0 && r.find("def f(x):") != std::string::npos) y_def = y;
        if (y_ret < 0 && r.find("return x * 2") != std::string::npos) y_ret = y;
        if (y_nosuch < 0 && r.find("return 1") != std::string::npos) y_nosuch = y;
        if (y_bare < 0 && r.find("return 2") != std::string::npos) y_bare = y;
      }
      check(y_def > 0 && a.is(y_def, "def", th.of(ROLLTUI_SYN_KEYWORD)) && a.is(y_ret, "return", th.of(ROLLTUI_SYN_KEYWORD)) && a.is(y_ret, "# double", th.of(ROLLTUI_SYN_COMMENT)),
            "a ```python fence is coloured: a keyword and a comment [rows " + std::to_string(y_def) + "," + std::to_string(y_ret) + ": " + a.row(y_ret) + "]");
      check(y_nosuch > 0 && coloured_cells(a, th) > 0 && !same(a.style(a.find(y_nosuch, "return"), y_nosuch).fg, th.of(ROLLTUI_SYN_KEYWORD)), "a fence that names no language the library knows stays plain");
      check(y_bare > 0 && !same(a.style(a.find(y_bare, "return"), y_bare).fg, th.of(ROLLTUI_SYN_KEYWORD)), "…and so does a bare fence");
      // the block's own ground under a coloured word: the same as under the plain text beside it
      const RolltuiStyleColor block_bg = a.style(a.find(y_ret, "x * 2"), y_ret).bg;
      check(same(a.style(a.find(y_def, "def"), y_def).bg, a.style(a.find(y_def, "(x)"), y_def).bg) && same(a.style(a.find(y_ret, "return"), y_ret).bg, block_bg) &&
                same(a.style(a.find(y_ret, "# double"), y_ret).bg, block_bg),
            "coloured code in a code block keeps the block's background: no holes in the shading");
      check(b.text() == a.text() && coloured_cells(b, th) < coloured_cells(a, th), "(and with no languages lent the same fence has fewer coloured cells)");
    }

    // ---- EVERY SHIPPED THEME, IN BOTH MODES: what the code looks like is what the theme says, and it is legible in each -----------------------------------
    {
      // the style a class is drawn in, exactly as the preview builds it: the theme's role, the window's ground, and the class's own bold or italic on top
      auto look = [](const RolltuiStyle* styles, unsigned char cls) {
        RolltuiStyle st = styles[rolltui_syntax_role(cls, ROLLTUI_ROLE_TEXT)];
        st.bg = styles[ROLLTUI_ROLE_BACKGROUND].bg;
        const unsigned char fl = rolltui_syntax_flags(cls);
        if (fl & ROLLTUI_SYN_BOLD) st.bold = 1;
        if (fl & ROLLTUI_SYN_ITALIC) st.italic = 1;
        return st;
      };
      auto differ = [](const RolltuiStyle& a, const RolltuiStyle& b) {
        return !same(a.fg, b.fg) || a.bold != b.bold || a.italic != b.italic || a.underline != b.underline || a.dim != b.dim || a.reverse != b.reverse;
      };
      auto lum = [](const RolltuiStyleColor& c) {
        auto ch = [](int v) { const double x = v / 255.0; return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b);
      };
      auto contrast = [&](const RolltuiStyleColor& a, const RolltuiStyleColor& b) {
        const double la = lum(a), lb = lum(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
      };
      const unsigned char core[] = {ROLLTUI_SYN_KEYWORD, ROLLTUI_SYN_TYPE, ROLLTUI_SYN_FUNCTION, ROLLTUI_SYN_STRING, ROLLTUI_SYN_NUMBER, ROLLTUI_SYN_COMMENT};
      int themes = 0, looked = 0;
      std::string bad, weakest;
      double weakest_ratio = 1000;
      for (const auto& entry : fs::directory_iterator(ROLLTUI_PRESETS_DIR)) {
        if (entry.path().extension() != ".json") continue;
        const std::string text = slurp(entry.path());
        RolltuiStr err{};
        RolltuiJsonValue* root = rolltui_json_parse(text.data(), text.size(), &err);
        rolltui_str_free(&err);
        const RolltuiJsonValue* colours = root ? rolltui_json_get(root, "colours", 7) : nullptr;
        if (!colours) { rolltui_json_free(root); continue; }
        ++themes;
        for (int mode : {ROLLTUI_MODE_DARK, ROLLTUI_MODE_LIGHT}) {
          RolltuiStyle styles[ROLLTUI_ROLE_COUNT]{};
          RolltuiStr name{};
          RolltuiThemeReport report{};
          RolltuiEffectMap* fx = rolltui_theme_load(colours, mode, rolltui_theme_default_vocab(), styles, &name, &report);
          const std::string id = entry.path().stem().string() + (mode == ROLLTUI_MODE_DARK ? "/dark" : "/light");
          rolltui_str_free(&name);
          rolltui_theme_report_release(&report);
          rolltui_effect_map_free(fx);
          ++looked;
          const RolltuiStyleColor ground = styles[ROLLTUI_ROLE_BACKGROUND].bg;
          const RolltuiStyle plain = look(styles, ROLLTUI_SYN_PLAIN);
          // 1. the keyword is bold and the comment is italic, whatever the theme did or did not put on its roles
          if (!look(styles, ROLLTUI_SYN_KEYWORD).bold) bad += " " + id + ": keyword not bold;";
          if (!look(styles, ROLLTUI_SYN_COMMENT).italic) bad += " " + id + ": comment not italic;";
          // 2. keyword, comment, string, and plain text are each told from each other by colour or by weight, slant, underline or dimness
          for (unsigned char a : core) {
            if (!differ(look(styles, a), plain)) bad += " " + id + ": " + rolltui_syntax_class_name(a) + " is drawn as plain text;";
            for (unsigned char b : core)
              if (a < b && !differ(look(styles, a), look(styles, b)) && !(a == ROLLTUI_SYN_TYPE && b == ROLLTUI_SYN_NUMBER)) bad += " " + id + ": " + rolltui_syntax_class_name(a) + " = " + rolltui_syntax_class_name(b) + ";";
          }
          // 3. with colour, none of it is the colour of the ground; and the weakest contrast anywhere is noted
          if (ground.kind != RolltuiStyleColor::Kind::None)
            for (unsigned char a : core) {
              const RolltuiStyleColor fg = look(styles, a).fg;
              if (fg.kind == RolltuiStyleColor::Kind::None) continue;
              if (same(fg, ground)) bad += " " + id + ": " + rolltui_syntax_class_name(a) + " is the colour of the ground;";
              if (fg.kind == RolltuiStyleColor::Kind::Rgb && ground.kind == RolltuiStyleColor::Kind::Rgb) {
                const double r = contrast(fg, ground);
                if (r < weakest_ratio) { weakest_ratio = r; weakest = id + " " + rolltui_syntax_class_name(a); }
              }
            }
          // 4. and every class stands on the ground, not on a role's own patch of colour
          for (int c = 0; c < ROLLTUI_SYN_COUNT; ++c)
            if (!same(look(styles, static_cast<unsigned char>(c)).bg, ground)) bad += " " + id + ": class " + rolltui_syntax_class_name(static_cast<unsigned char>(c)) + " has another ground;";
        }
        rolltui_json_free(root);
      }
      check(themes >= 8 && looked == themes * 2, "the shipped theme files are found (" + std::to_string(themes) + " themes, " + std::to_string(looked) + " looks)");
      check(bad.empty(), "in every shipped theme, both modes: a keyword is bold, a comment italic, and keyword, type, function, string, number, comment and plain text are told apart [" + bad + "]");
      check(weakest_ratio >= 2.0, "…and none of them is close to invisible: the weakest is " + std::to_string(weakest_ratio).substr(0, 4) + ":1 (" + weakest + ": that theme's own accent, the colour it draws its hints in too)");
    }

    // ---- A MARKDOWN DOCUMENT, TOGGLED: the code in it is coloured, plain, and coloured again as the languages come and go ---------------------------------------
    {
      write_file(root / "toggle.md", "text\n\n```c\nreturn 1;\n```\n");
      Pv pv(syn);
      pv.show(root / "toggle.md");
      auto kw_colour = [&](const Shot& s) {
        for (int y = 0; y < s.h; ++y)
          if (s.row(y).find("return 1;") != std::string::npos) return s.is(y, "return", th.of(ROLLTUI_SYN_KEYWORD));
        return false;
      };
      { Shot s(pv.pv, th, 40, 12); check(kw_colour(s), "a fenced C block is coloured"); }
      rolltui_preview_set_syntax(pv.pv, nullptr);
      { Shot s(pv.pv, th, 40, 12); check(!kw_colour(s) && s.text().find("return 1;") != std::string::npos, "taking the languages back draws the same block plain, laid out again, with the same words"); }
      rolltui_preview_set_syntax(pv.pv, syn);
      { Shot s(pv.pv, th, 40, 12); check(kw_colour(s), "…and lending them again colours it"); }
      Shot bold(pv.pv, th, 40, 12);
      int y = -1;
      for (int r = 0; r < 12; ++r) if (bold.row(r).find("return 1;") != std::string::npos) y = r;
      check(y >= 0 && bold.style(bold.find(y, "return"), y).bold && !bold.style(bold.find(y, "1;"), y).bold, "…the keyword in it is bold and the number beside it is not: the attribute rides through the Markdown renderer");
    }

    // ---- THE PICKER: `no_syntax` ---------------------------------------------------------------------------------------------------------------
    {
      const fs::path tree = root / "tree";
      write_file(tree / "code.c", "return 1; // done\n");
      timespec ts[2];
      ts[0].tv_sec = ts[1].tv_sec = ::time(nullptr) - 3600;
      ts[0].tv_nsec = ts[1].tv_nsec = 0;
      ::utimensat(AT_FDCWD, (tree / "code.c").c_str(), ts, 0);
      ::utimensat(AT_FDCWD, tree.c_str(), ts, 0);
      RolltuiContext* ctx = rolltui_context_new();
      rolltui_context_set_library_defaults(ctx);
      const RolltuiBindings* bind = rolltui_bindings_default(ctx);
      const RolltuiPickerActions* actions = rolltui_picker_default_actions();
      RolltuiScrollbarGlyphs glyphs{};
      rolltui_theme_scrollbar_glyphs(nullptr, &glyphs);
      RolltuiPicker* p = rolltui_picker_new();
      RolltuiPickerOptions po{};
      rolltui_picker_options_init(&po);
      check(po.no_syntax == 0, "colour is on in a zeroed options struct: it is spelled as an opt-out");
      po.motion = 0;
      po.preview = ROLLTUI_PREVIEW_RIGHT;
      po.no_watch = 1;
      rolltui_picker_set_options(p, &po);
      const std::string dir = tree.string();
      rolltui_picker_go_to(p, dir.data(), dir.size());
      auto shot = [&](Theme& t) {
        RolltuiFrame* f = rolltui_frame_new(90, 8, t.styles[ROLLTUI_ROLE_BACKGROUND]);
        rolltui_picker_layout(p, RolltuiRect{0, 0, 90, 8});
        rolltui_picker_draw(p, f, t.styles, &glyphs, 0);
        // the coloured cells anywhere on the frame that are the keyword's colour
        int kw = 0;
        for (int y = 0; y < 8; ++y)
          for (int x = 0; x < 90; ++x) {
            RolltuiCell c;
            rolltui_frame_cell(f, x, y, &c);
            if (same(c.style.fg, t.of(ROLLTUI_SYN_KEYWORD)) && std::string(c.bytes, c.len) == "r") ++kw;
          }
        RolltuiStr s{};
        rolltui_frame_to_text(f, &s);
        const std::string text(s.p ? s.p : "", s.n);
        rolltui_str_free(&s);
        rolltui_frame_free(f);
        return std::make_pair(kw, text);
      };
      Theme t;
      RolltuiEvent right{};
      right.kind = ROLLTUI_EVENT_KEY;
      right.key.key = ROLLTUI_KEY_RIGHT;
      (void)shot(t);
      rolltui_picker_handle(p, &right, bind, actions);
      auto on = shot(t);
      check(on.second.find("code.c") != std::string::npos && on.first == 2 && on.second.find("C \xC2\xB7") != std::string::npos, "the picker previews a C file with its `return` coloured, and the head says C [" + std::to_string(on.first) + " keyword cells]");
      po.no_syntax = 1;
      rolltui_picker_set_options(p, &po);
      auto off = shot(t);
      check(off.first == 0 && off.second.find("text \xC2\xB7") != std::string::npos, "with `no_syntax` the same file is plain at the next frame, and is `text` again");
      po.no_syntax = 0;
      rolltui_picker_set_options(p, &po);
      auto again = shot(t);
      check(again.first == 2, "…and turning it back on colours it again at once");
      rolltui_picker_free(p);
      rolltui_context_free(ctx);
    }
    rolltui_syntax_free(syn);
  }
  fs::remove_all(root, ec);
  check(live_bytes() == base, "previews, pickers and languages made and freed: the library holds what it held before (" + std::to_string(live_bytes()) + " vs " + std::to_string(base) + ")");
  return testkit::report("rolltui_preview_syntax_test");
}
