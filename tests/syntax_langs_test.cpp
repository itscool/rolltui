// rolltui/tests/syntax_langs_test.cpp — THE SHIPPED LANGUAGES, AND THE PICTURES THEY DRAW.
//
// `syntax_test.cpp` holds the ENGINE to what it promises with small languages written there. This holds what SHIPS: that
// every language file loads and none is refused; that a file is found by its name, its extension or its first line, and a
// fence by its tag; and, for every language, THE PICTURE — a sample of real code in `tests/fixtures/syntax/<name>.sample` is
// run and shown as `{class|text}` and compared, line for line, with `<name>.sample.expected`. A picture can only be judged by
// looking, so the fixtures are READ before they are recorded: `rolltui-syntax-langs-test --record` rewrites them, and a diff
// in one is either a colouring that got better or one that broke.
//
// AROUND THEM, what no picture shows: that a CRLF file, a file with no final newline and the first lines of a file all
// colour as they do in the whole; that no run ends inside a letter; that a language cannot be made to hang or crash by
// damaged code or by the lines a regex hates; that a person's own file, in the configuration directory, adds a language,
// replaces a shipped one, and that one broken file takes nothing else with it.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_syntax.h"  /* INTERNAL: this suite is in ROLLTUI_INTERNAL_OPT_IN */
#include "rolltui_test.hpp"
#include "syntax_test_helpers.hpp"

namespace fs = std::filesystem;
using testkit::check;
using syntest::add;
using syntest::lang_of;
using syntest::live_bytes;
using syntest::marked;

namespace {

struct Fixture {
  const char* sample;    // tests/fixtures/syntax/<sample>.sample
  const char* path;      // the name the file is found by: the detection is part of what is held
  const char* language;  // what it must be found as
};

const Fixture kFixtures[] = {
    {"c", "main.c", "C"},
    {"cpp", "widget.hpp", "C++"},
    {"java", "Widget.java", "Java"},
    {"csharp", "Thing.cs", "C#"},
    {"javascript", "app.mjs", "JavaScript"},
    {"typescript", "app.ts", "TypeScript"},
    {"python", "app.py", "Python"},
    {"rust", "main.rs", "Rust"},
    {"go", "main.go", "Go"},
    {"swift", "Thing.swift", "Swift"},
    {"lua", "mod.lua", "Lua"},
    {"luau", "mod.luau", "Luau"},
    {"shell", "build.sh", "Shell"},
    {"batch", "build.bat", "Batch"},
    {"powershell", "Get-Thing.ps1", "PowerShell"},
    {"cmake", "CMakeLists.txt", "CMake"},
    {"json", "package.json", "JSON"},
    {"yaml", "ci.yml", "YAML"},
    {"ini", "config.ini", "INI"},
    {"toml", "Cargo.toml", "TOML"},
    {"xml", "pom.xml", "XML"},
    {"css", "site.css", "CSS"},
    {"html", "index.html", "HTML"},
    {"ignore", ".gitignore", "Ignore"},
    {"gitattributes", ".gitattributes", "Git Attributes"},
};

std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void write_named(const fs::path& p, const std::string& t) { std::ofstream(p, std::ios::binary | std::ios::trunc) << t; }

std::string first_line_of(const std::string& text) { return text.substr(0, text.find('\n')); }

// what every set of runs must be, for any bytes at all
bool sound(RolltuiHighlight* h, const std::string& text, std::string* why) {
  std::size_t at = 0;
  for (std::size_t li = 0; li < rolltui_highlight_line_count(h); ++li) {
    const std::size_t nl = text.find('\n', at);
    std::size_t len = (nl == std::string::npos ? text.size() : nl) - at;
    if (len > 0 && text[at + len - 1] == '\r') --len;
    const RolltuiSyntaxRun* runs = nullptr;
    const std::size_t n = rolltui_highlight_line(h, li, &runs);
    unsigned prev = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const bool ok = runs[i].begin >= prev && runs[i].end > runs[i].begin && runs[i].end <= len && runs[i].cls != ROLLTUI_SYN_PLAIN && runs[i].cls < ROLLTUI_SYN_COUNT &&
                      (runs[i].begin >= len || (static_cast<unsigned char>(text[at + runs[i].begin]) & 0xC0) != 0x80) &&
                      (runs[i].end >= len || (static_cast<unsigned char>(text[at + runs[i].end]) & 0xC0) != 0x80);
      if (!ok) {
        if (why) *why = "line " + std::to_string(li) + " run " + std::to_string(i) + " [" + std::to_string(runs[i].begin) + "," + std::to_string(runs[i].end) + ") of " + std::to_string(len);
        return false;
      }
      prev = runs[i].end;
    }
    at = nl == std::string::npos ? text.size() : nl + 1;
  }
  return true;
}

std::size_t classes_used(RolltuiSyntax* s, int lang, const std::string& text) {
  RolltuiHighlight* h = rolltui_highlight_new();
  rolltui_highlight_run(h, s, lang, text.data(), text.size());
  std::set<int> used;
  for (std::size_t li = 0; li < rolltui_highlight_line_count(h); ++li) {
    const RolltuiSyntaxRun* runs = nullptr;
    const std::size_t n = rolltui_highlight_line(h, li, &runs);
    for (std::size_t i = 0; i < n; ++i) used.insert(runs[i].cls);
  }
  rolltui_highlight_free(h);
  return used.size();
}

std::string lower(std::string s) {
  for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  return s;
}

std::string name_of(RolltuiSyntax* s, int lang) {
  const char* n = rolltui_syntax_language_name(s, lang);
  return n ? n : "<none>";
}

double ms_since(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }

}  // namespace

int main(int argc, char** argv) {
  const bool record = argc > 1 && std::strcmp(argv[1], "--record") == 0;
  const fs::path dir = fs::path(ROLLTUI_FIXTURE_DIR) / "syntax";
  const std::size_t base = live_bytes();
  {
    // ---- EVERY SHIPPED LANGUAGE LOADS, AND NONE IS REFUSED ---------------------------------------------------------------------------------------
    RolltuiSyntax* s = rolltui_syntax_new();
    RolltuiStr report{};
    rolltui_syntax_add_shipped(s, &report);
    check(report.n == 0, "every shipped language file loads [" + std::string(report.p ? report.p : "", report.n) + "]");
    rolltui_str_free(&report);
    check(rolltui_kSyntaxPresetCount >= 25 && rolltui_syntax_language_count(s) == rolltui_kSyntaxPresetCount,
          "…and each is a language of its own (" + std::to_string(rolltui_syntax_language_count(s)) + " languages from " + std::to_string(rolltui_kSyntaxPresetCount) + " files)");
    {
      std::set<std::string> names;
      for (std::size_t i = 0; i < rolltui_syntax_language_count(s); ++i) names.insert(lower(name_of(s, static_cast<int>(i))));
      check(names.size() == rolltui_syntax_language_count(s), "…with no two of one name");
    }
    for (const char* want : {"C", "C++", "C#", "Java", "JavaScript", "TypeScript", "Python", "Rust", "Go", "Swift", "Lua", "Luau", "Shell", "Batch", "PowerShell", "CMake", "HTML", "CSS", "XML", "JSON",
                             "YAML", "INI", "TOML", "Ignore", "Git Attributes"})
      check(lang_of(s, want) >= 0 && name_of(s, lang_of(s, want)) == want, std::string("the language `") + want + "` is shipped");

    // ---- A FILE IS FOUND BY ITS NAME, ITS EXTENSION OR ITS FIRST LINE ----------------------------------------------------------------------------------------
    struct Found { const char* path; const char* first; const char* lang; };
    const Found kFound[] = {
        {"main.c", "", "C"}, {"a/b/util.h", "", "C++"}, {"x.hpp", "", "C++"}, {"x.cc", "", "C++"}, {"x.cxx", "", "C++"}, {"Foo.java", "", "Java"}, {"Foo.cs", "", "C#"}, {"x.csx", "", "C#"},
        {"a.js", "", "JavaScript"}, {"a.mjs", "", "JavaScript"}, {"a.cjs", "", "JavaScript"}, {"a.jsx", "", "JavaScript"}, {"a.ts", "", "TypeScript"}, {"a.tsx", "", "TypeScript"}, {"lib.d.ts", "", "TypeScript"},
        {"a.py", "", "Python"}, {"a.pyi", "", "Python"}, {"BUILD.bazel", "", "Python"}, {"a.rs", "", "Rust"}, {"a.go", "", "Go"}, {"go.mod", "", "Go"}, {"a.swift", "", "Swift"}, {"Package.swift", "", "Swift"},
        {"a.lua", "", "Lua"}, {"a.luau", "", "Luau"}, {"a.sh", "", "Shell"}, {"a.bash", "", "Shell"}, {"a.zsh", "", "Shell"}, {".bashrc", "", "Shell"}, {".zshrc", "", "Shell"}, {".profile", "", "Shell"},
        {"a.bat", "", "Batch"}, {"a.cmd", "", "Batch"}, {"A.BAT", "", "Batch"}, {"a.ps1", "", "PowerShell"}, {"a.psm1", "", "PowerShell"}, {"CMakeLists.txt", "", "CMake"}, {"/x/y/a.cmake", "", "CMake"},
        {"a.html", "", "HTML"}, {"a.htm", "", "HTML"}, {"a.xml", "", "XML"}, {"a.svg", "", "XML"}, {"a.plist", "", "XML"}, {"pom.xml", "", "XML"}, {"a.css", "", "CSS"}, {"a.json", "", "JSON"},
        {"tsconfig.json", "", "JSON"}, {".babelrc", "", "JSON"}, {"a.jsonc", "", "JSON"}, {"a.yaml", "", "YAML"}, {"a.yml", "", "YAML"}, {"a.ini", "", "INI"}, {".editorconfig", "", "INI"},
        {".gitconfig", "", "INI"}, {".gitmodules", "", "INI"}, {"setup.cfg", "", "INI"}, {".env", "", "INI"}, {"a.toml", "", "TOML"}, {"Cargo.lock", "", "TOML"}, {".gitignore", "", "Ignore"},
        {".dockerignore", "", "Ignore"}, {".npmignore", "", "Ignore"}, {"/x/.eslintignore", "", "Ignore"}, {".gitattributes", "", "Git Attributes"},
        {"run", "#!/usr/bin/env python3", "Python"}, {"run", "#!/bin/bash", "Shell"}, {"run", "#!/bin/sh", "Shell"}, {"run", "#!/usr/bin/env node", "JavaScript"}, {"run", "#!/usr/bin/lua", "Lua"},
        {"run", "#!/usr/bin/env pwsh", "PowerShell"}, {"run", "--!strict", "Luau"}, {"data", "<?xml version=\"1.0\"?>", "XML"}, {"page", "<!DOCTYPE html>", "HTML"}, {"data", "%YAML 1.2", "YAML"},
        {"README", "", ""}, {"Makefile", "", ""}, {"a.txt", "", ""}, {"a.md", "", ""}, {"noext", "", ""}, {".ts", "", ""}, {"a.", "", ""}, {"", "", ""}, {"run", "#!/usr/bin/perl", ""}, {"a.unknownext", "", ""},
    };
    for (const Found& f : kFound) {
      const int l = rolltui_syntax_find_file(s, f.path, std::strlen(f.path), *f.first ? f.first : nullptr, std::strlen(f.first));
      const std::string got = l < 0 ? "" : name_of(s, l);
      check(got == f.lang, std::string("`") + f.path + "`" + (*f.first ? std::string(" (") + f.first + ")" : "") + " is " + (*f.lang ? f.lang : "no language") + " [" + (got.empty() ? "none" : got) + "]");
    }

    // ---- A FENCE IS FOUND BY ITS TAG --------------------------------------------------------------------------------------------------------------------------
    struct Tag { const char* tag; const char* lang; };
    const Tag kTags[] = {
        {"c", "C"}, {"C", "C"}, {"c++", "C++"}, {"cpp", "C++"}, {"cc", "C++"}, {"hpp", "C++"}, {"h", "C++"}, {"java", "Java"}, {"cs", "C#"}, {"csharp", "C#"}, {"c#", "C#"}, {"js", "JavaScript"},
        {"javascript", "JavaScript"}, {"jsx", "JavaScript"}, {"node", "JavaScript"}, {"ts", "TypeScript"}, {"typescript", "TypeScript"}, {"tsx", "TypeScript"}, {"py", "Python"}, {"python", "Python"},
        {"python3", "Python"}, {"rust", "Rust"}, {"rs", "Rust"}, {"go", "Go"}, {"golang", "Go"}, {"swift", "Swift"}, {"lua", "Lua"}, {"luau", "Luau"}, {"sh", "Shell"}, {"bash", "Shell"}, {"zsh", "Shell"},
        {"shell", "Shell"}, {"ksh", "Shell"}, {"bat", "Batch"}, {"cmd", "Batch"}, {"batch", "Batch"}, {"dos", "Batch"}, {"powershell", "PowerShell"}, {"ps1", "PowerShell"}, {"pwsh", "PowerShell"},
        {"cmake", "CMake"}, {"html", "HTML"}, {"htm", "HTML"}, {"xml", "XML"}, {"svg", "XML"}, {"json", "JSON"}, {"jsonc", "JSON"}, {"json5", "JSON"}, {"yaml", "YAML"}, {"yml", "YAML"}, {"ini", "INI"},
        {"cfg", "INI"}, {"toml", "TOML"}, {"css", "CSS"}, {"gitignore", "Ignore"}, {"dockerignore", "Ignore"}, {"ignore", "Ignore"}, {"gitattributes", "Git Attributes"},
        {"", ""}, {"cobol", ""}, {"text", ""}, {"plain", ""}, {"markdown", ""}, {"diff", ""}, {"mermaid", ""},
    };
    for (const Tag& t : kTags) {
      const int l = rolltui_syntax_find_name(s, t.tag, std::strlen(t.tag));
      const std::string got = l < 0 ? "" : name_of(s, l);
      check(got == t.lang, std::string("the fence ```") + t.tag + " is " + (*t.lang ? t.lang : "no language") + " [" + (got.empty() ? "none" : got) + "]");
    }

    // ---- THE PICTURES ------------------------------------------------------------------------------------------------------------------------------------
    for (const Fixture& fx : kFixtures) {
      const fs::path src = dir / (std::string(fx.sample) + ".sample");
      const fs::path golden = dir / (std::string(fx.sample) + ".sample.expected");
      const std::string text = slurp(src);
      const std::string label = std::string(fx.sample);
      check(!text.empty(), label + ": the sample is there");
      if (text.empty()) continue;
      const int lang = rolltui_syntax_find_file(s, fx.path, std::strlen(fx.path), first_line_of(text).c_str(), first_line_of(text).size());
      check(lang >= 0 && name_of(s, lang) == fx.language, label + ": `" + fx.path + "` is found as " + fx.language + " [" + name_of(s, lang) + "]");
      if (lang < 0) continue;
      const std::string got = marked(s, lang, text) + "\n";
      if (record) {
        std::ofstream(golden, std::ios::binary | std::ios::trunc) << got;
        std::printf("recorded %s\n", golden.string().c_str());
        continue;
      }
      const std::string want = slurp(golden);
      check(got == want, label + ": the picture is the recorded one");
      if (got != want) {
        std::istringstream a(got), b(want);
        std::string la, lb;
        int row = 0;
        while (true) {
          const bool ga = static_cast<bool>(std::getline(a, la)), gb = static_cast<bool>(std::getline(b, lb));
          ++row;
          if (!ga && !gb) break;
          if (la != lb || ga != gb) {
            std::fprintf(stderr, "  first difference at line %d\n    got  [%s]\n    want [%s]\n", row, ga ? la.c_str() : "(none)", gb ? lb.c_str() : "(none)");
            break;
          }
        }
      }
      check(classes_used(s, lang, text) >= 3, label + ": the sample shows at least three kinds of text coloured (" + std::to_string(classes_used(s, lang, text)) + ")");

      // the same code, written by another editor
      std::string crlf;
      for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
      check(marked(s, lang, crlf) + "\n" == got, label + ": a CRLF file colours as the LF one does");
      std::string nofinal = text;
      while (!nofinal.empty() && nofinal.back() == '\n') nofinal.pop_back();
      check(marked(s, lang, nofinal) + "\n" == got, label + ": a file with no final newline colours as with one");

      // colour flows FORWARD only: the first lines of a file are coloured as they are in the whole file
      {
        std::size_t upto = 0;
        bool prefixes_agree = true;
        for (int lines = 3; lines < 40 && upto < text.size(); lines += 5) {
          upto = 0;
          for (int k = 0; k < lines && upto < text.size(); ++k) { const std::size_t nl = text.find('\n', upto); upto = nl == std::string::npos ? text.size() : nl + 1; }
          const std::string head = marked(s, lang, text.substr(0, upto));
          if (got.compare(0, head.size(), head) != 0) { prefixes_agree = false; break; }
        }
        check(prefixes_agree, label + ": the first lines of the file are coloured as they are in the whole of it");
      }

      // every prefix of the file, cut anywhere (inside a string, a comment, a tag, a letter): sound runs
      {
        RolltuiHighlight* h = rolltui_highlight_new();
        bool all_sound = true;
        std::string why;
        for (std::size_t cut = 0; cut <= text.size() && all_sound; cut += 3) {
          rolltui_highlight_run(h, s, lang, text.data(), cut);
          all_sound = sound(h, text.substr(0, cut), &why);
        }
        check(all_sound, label + ": cut anywhere, the runs are sound [" + why + "]");
        rolltui_highlight_free(h);
      }
    }

    // ---- DAMAGED CODE: every language, its own sample mutated, and text full of the characters languages are made of ------------------------------------------------
    {
      std::uint32_t seed = 20260920;
      auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
      const std::string glue = "\"'`/*#<>{}()[]$\\=:;-!%&|@~ \t\n\xC3\xA9\xE2\x82\xAC";
      RolltuiHighlight* h = rolltui_highlight_new();
      bool all_sound = true;
      std::string why, culprit;
      std::size_t runs_made = 0;
      for (const Fixture& fx : kFixtures) {
        const std::string text = slurp(dir / (std::string(fx.sample) + ".sample"));
        const int lang = lang_of(s, fx.language);
        for (int round = 0; round < 60 && all_sound; ++round) {
          std::string t = text;
          const int edits = 1 + static_cast<int>(rnd() % 12);
          for (int e = 0; e < edits && !t.empty(); ++e) {
            const std::size_t at = rnd() % t.size();
            switch (rnd() % 3) {
              case 0: t[at] = glue[rnd() % glue.size()]; break;
              case 1: t.erase(at, 1 + rnd() % 8); break;
              default: t.insert(at, 1, glue[rnd() % glue.size()]); break;
            }
          }
          rolltui_highlight_run(h, s, lang, t.data(), t.size());
          ++runs_made;
          if (!sound(h, t, &why)) { all_sound = false; culprit = std::string(fx.language) + ": " + why; }
        }
        // and pure noise, from the same alphabet
        for (int round = 0; round < 25 && all_sound; ++round) {
          std::string t;
          const int n = static_cast<int>(rnd() % 400);
          for (int i = 0; i < n; ++i) t.push_back(glue[rnd() % glue.size()]);
          rolltui_highlight_run(h, s, lang, t.data(), t.size());
          ++runs_made;
          if (!sound(h, t, &why)) { all_sound = false; culprit = std::string(fx.language) + " (noise): " + why; }
        }
      }
      check(all_sound, "damaged code and noise, " + std::to_string(runs_made) + " runs across every language: runs are sound [" + culprit + "]");
      rolltui_highlight_free(h);
    }

    // ---- THE LINES A PATTERN HATES: no language may be made slow by a line ---------------------------------------------------------------------------------------
    {
      struct Hate { const char* name; std::string text; };
      auto rep = [](const std::string& unit, int n) { std::string r; for (int i = 0; i < n; ++i) r += unit; return r; };
      std::vector<Hate> hates = {
          {"a x4000", rep("a", 4000)},        {"\" x4000", rep("\"", 4000)},     {"' x4000", rep("'", 4000)},     {"/* x2000", rep("/*", 2000)},   {"( x4000", rep("(", 4000)},
          {"< x4000", rep("<", 4000)},        {"<a x2000", rep("<a", 2000)},     {"a< x2000", rep("a<", 2000)},   {"\\ x4000", rep("\\", 4000)},   {"$ x4000", rep("$", 4000)},
          {"${ x1300", rep("${", 1300)},      {"\\\" x1300", rep("\\\"", 1300)}, {"- x2000", rep("- ", 2000)},    {"#[ x2000", rep("#[", 2000)},   {"[[= x1300", rep("[[=", 1300)},
          {"space x4000", rep(" ", 4000)},    {"x: x2000", rep("x:", 2000)},     {"a: x1300", rep("a: ", 1300)},  {"1.2. x1300", rep("1.2.", 1000)}, {"a.b x1300", rep("a.b(", 1000)},
          {"<a b=' x1000", rep("<a b='", 1000)}, {"# x4000", rep("#", 4000)},   {"@\" x2000", rep("@\"", 2000)}, {"`{ x2000", rep("`{", 2000)},  {"{{ x2000", rep("{{", 2000)},
          {"lines", rep("if (a) { b = \"c\\n\"; } // d\n<x y=\"z\">\n", 400)},
      };
      double worst = 0;
      std::string worst_case;
      RolltuiHighlight* h = rolltui_highlight_new();
      for (const Fixture& fx : kFixtures) {
        const int lang = lang_of(s, fx.language);
        for (const Hate& hate : hates) {
          const auto t0 = std::chrono::steady_clock::now();
          rolltui_highlight_run(h, s, lang, hate.text.data(), hate.text.size());
          const double ms = ms_since(t0);
          if (ms > worst) { worst = ms; worst_case = std::string(fx.language) + " on " + hate.name; }
        }
      }
      check(worst < 800.0, "no language is slow on a line it hates: the worst is " + std::to_string(static_cast<int>(worst)) + " ms (" + worst_case + ")");
      rolltui_highlight_free(h);
    }

    // ---- SPEED: a quarter of a megabyte of each language's own sample -----------------------------------------------------------------------------------------------------
    {
      double worst = 0;
      std::string worst_lang;
      RolltuiHighlight* h = rolltui_highlight_new();
      for (const Fixture& fx : kFixtures) {
        const std::string text = slurp(dir / (std::string(fx.sample) + ".sample"));
        std::string big;
        while (big.size() < 262144) big += text;
        const auto t0 = std::chrono::steady_clock::now();
        rolltui_highlight_run(h, s, lang_of(s, fx.language), big.data(), big.size());
        const double ms = ms_since(t0);
        if (ms > worst) { worst = ms; worst_lang = fx.language; }
      }
      check(worst < 1500.0, "a quarter of a megabyte of code in each language: the slowest is " + std::to_string(static_cast<int>(worst)) + " ms (" + worst_lang + ")");
      rolltui_highlight_free(h);
    }

    // ---- THE README'S EXAMPLE LANGUAGE LOADS AND WORKS: an example in documentation that stops loading is a lie ---------------------------------------------------
    {
      const std::string readme = slurp(fs::path(ROLLTUI_SOURCE_DIR) / "README.md");
      const std::size_t sec = readme.find("## Source colour");
      const std::size_t open = readme.find("```json\n", sec);
      const std::size_t close = open == std::string::npos ? std::string::npos : readme.find("\n```", open + 8);
      check(sec != std::string::npos && open != std::string::npos && close != std::string::npos, "the README has a Source colour section with a JSON example");
      if (close != std::string::npos) {
        std::string why;
        RolltuiSyntax* t = rolltui_syntax_new_standard(nullptr);
        check(add(t, readme.substr(open + 8, close - (open + 8)), &why), "…and the example loads [" + why + "]");
        const int mine = lang_of(t, "Mine");
        check(mine >= 0 && marked(t, mine, "if x then f(1) # note\nelse \"s\\n\"") == "{keyword|if} x {keyword|then} {function|f}({number|1}) {comment|# note}\n{keyword|else} {string|\"s}{escape|\\n}{string|\"}",
              "…and does what the section says it does: " + (mine >= 0 ? marked(t, mine, "if x then f(1) # note\nelse \"s\\n\"") : std::string("<absent>")));
        check(rolltui_syntax_find_file(t, "Minefile", 8, nullptr, 0) == mine && rolltui_syntax_find_file(t, "a.mine", 6, nullptr, 0) == mine, "…and is found by the file name and the extension it says");
        rolltui_syntax_free(t);
      }
    }

    // ---- A LANGUAGE THAT INCLUDES ANOTHER FOLLOWS IT, if a person changes the one it includes --------------------------------------------------------------------------
    {
      check(marked(s, lang_of(s, "C++"), "int x;") == "{type|int} x;", "C++ colours `int` as a type, from C's rules");
      check(add(s, R"J({ "name": "c", "extensions": ["c"], "contexts": { "main": [ { "words": ["int"], "class": "keyword" } ] } })J"), "a person's own `c` (any case) loads");
      check(marked(s, lang_of(s, "C++"), "int x;") == "{keyword|int} x;", "…and C++, which includes C by name, follows it: " + marked(s, lang_of(s, "C++"), "int x;"));
      check(marked(s, lang_of(s, "C"), "int x;") == "{keyword|int} x;", "…and C is theirs");
      check(marked(s, lang_of(s, "TypeScript"), "const x = 1") == "{keyword|const} x = {number|1}", "TypeScript, which includes JavaScript, is unchanged by that");
    }
    rolltui_syntax_free(s);

    // ---- A PERSON'S OWN LANGUAGES, from the configuration directory -----------------------------------------------------------------------------------------------------
    {
      const fs::path root = fs::temp_directory_path() / ("rolltui-syntax-langs-" + std::to_string(::getpid()));
      const fs::path sdir = root / "rolltui" / "syntax";
      std::error_code ec;
      fs::remove_all(root, ec);
      fs::create_directories(sdir);
      auto put = [&](const std::string& name, const std::string& body) { std::ofstream(sdir / name, std::ios::binary | std::ios::trunc) << body; };
      put("zz-mine.json", R"J({ "name": "Mine", "aliases": ["mn"], "extensions": ["mine"], "contexts": { "main": [ { "words": ["hello", "world"], "class": "keyword" }, { "match": "#.*$", "class": "comment" } ] } })J");
      put("lua.json", R"J({ "name": "Lua", "extensions": ["lua"], "contexts": { "main": [ { "words": ["local"], "class": "type" } ] } })J");
      put("broken.json", R"J({ "name": "Broken", "contexts": { "main": [ { "match": "(oops", "class": "keyword" } ] } })J");
      put("nonsense.json", "this is not json at all");
      put("notes.txt", "a file that is not a language file");
      ::setenv("ROLL_CONFIG_DIR", root.c_str(), 1);
      RolltuiStr rep{};
      RolltuiSyntax* t = rolltui_syntax_new_standard(&rep);
      const std::string reported(rep.p ? rep.p : "", rep.n);
      rolltui_str_free(&rep);
      check(lang_of(t, "C") >= 0 && lang_of(t, "Rust") >= 0, "the standard set has the shipped languages");
      const int mine = rolltui_syntax_find_file(t, "x.mine", 6, nullptr, 0);
      check(mine >= 0 && name_of(t, mine) == "Mine" && lang_of(t, "mn") == mine, "a person's own language is found by its extension and its alias");
      check(marked(t, mine, "hello there # note") == "{keyword|hello} there {comment|# note}", "…and it colours: " + marked(t, mine, "hello there # note"));
      check(marked(t, lang_of(t, "Lua"), "local x = 1") == "{type|local} x = 1", "a person's `lua.json` REPLACES the shipped Lua: " + marked(t, lang_of(t, "Lua"), "local x = 1"));
      check(marked(t, lang_of(t, "Luau"), "local x = 1") == "{type|local} x = 1", "…and Luau, which includes Lua, follows it");
      check(lang_of(t, "Broken") < 0, "a file with a bad pattern adds nothing");
      check(reported.find("broken.json") != std::string::npos && reported.find("not closed") != std::string::npos, "…and the report names the file and why [" + reported + "]");
      check(reported.find("nonsense.json") != std::string::npos, "…and one that is not JSON at all is named too");
      check(reported.find("notes.txt") == std::string::npos && reported.find("zz-mine") == std::string::npos, "…and neither a file that is not a language file nor a good one is");
      rolltui_syntax_free(t);

      // ---- HOW A PERSON FINDS OUT: `rolltui_syntax_check` says which files loaded and why one did not -----------------------------------------------------------
      put("empty.json", "");
      put("huge.json", std::string(300 * 1024, ' '));
      RolltuiStr said{};
      const std::size_t bad_own = rolltui_syntax_check(nullptr, 0, &said);
      const std::string out = str_of(said);
      check(bad_own == 4, "four of the files in the folder did not load: the bad pattern, the one that is not JSON, the empty one, the huge one [" + std::to_string(bad_own) + "]");
      check(out.find("shipped: 25 languages") != std::string::npos, "it says how many are shipped: " + out.substr(0, 40));
      check(out.find("zz-mine.json  Mine\n") != std::string::npos, "…which of the person's own loaded");
      check(out.find("lua.json  Lua  (replaces the shipped one of that name)") != std::string::npos, "…and which replaced a shipped one");
      check(out.find("broken.json  NOT LOADED: Broken: context main, rule 1") != std::string::npos && out.find("not closed") != std::string::npos, "…and for a file that did not, its name, its language, the rule and the reason");
      check(out.find("nonsense.json  NOT LOADED: not JSON") != std::string::npos, "…a file that is not JSON");
      check(out.find("empty.json  NOT LOADED: the file is empty") != std::string::npos, "…an empty one, which was skipped without a word before");
      check(out.find("huge.json  NOT LOADED: the file is larger than 256 KB") != std::string::npos, "…and one too big to be a language, likewise");
      check(out.find("notes.txt") == std::string::npos, "…and a file that is not a `.json` is not mentioned");
      RolltuiStr rep_std{};
      RolltuiSyntax* again = rolltui_syntax_new_standard(&rep_std);
      const std::string std_rep = str_of(rep_std);
      check(std_rep.find("empty.json") != std::string::npos && std_rep.find("huge.json") != std::string::npos, "the report of the standard set names those two as well now: an empty file is a mistake too");
      rolltui_str_free(&rep_std);
      rolltui_syntax_free(again);
      write_named(root / "draft.json", R"J({ "name": "Draft", "extensions": ["drf"], "contexts": { "main": [ { "words": ["go"], "class": "keyword" } ] } })J");
      write_named(root / "typo.json", "{ \"name\": \"Typo\", \"contexts\": { \"main\": [ { \"match\": \"\\d+\", \"class\": \"number\" } ] } }");
      const std::string p_draft = (root / "draft.json").string(), p_typo = (root / "typo.json").string(), p_none = (root / "not-there.json").string();
      const char* paths[] = {p_draft.c_str(), p_typo.c_str(), p_none.c_str()};
      RolltuiStr said2{};
      const std::size_t bad_all = rolltui_syntax_check(paths, 3, &said2);
      const std::string out2 = str_of(said2);
      check(bad_all == bad_own + 2, "files named on the command line are checked too: two more that did not load");
      check(out2.find("draft.json: loaded as Draft\n") != std::string::npos, "a language being written is said to load, and as what");
      check(out2.find("typo.json: NOT LOADED: not JSON: line 1: unknown escape") != std::string::npos && out2.find("written twice") != std::string::npos, "…the usual mistake in one says what it was and what to write");
      check(out2.find("not-there.json: NOT LOADED: No such file") != std::string::npos, "…and a file that is not there says so");
      rolltui_str_free(&said);
      rolltui_str_free(&said2);

      ::setenv("ROLL_CONFIG_DIR", (root / "nowhere").c_str(), 1);
      RolltuiStr clean{};
      check(rolltui_syntax_check(nullptr, 0, &clean) == 0 && str_of(clean).find("none") != std::string::npos, "with no folder of a person's own, nothing to report: zero problems, and it says there are none");
      rolltui_str_free(&clean);
      ::setenv("ROLL_CONFIG_DIR", root.c_str(), 1);
      ::setenv("ROLL_CONFIG_DIR", (root / "nowhere").c_str(), 1);
      RolltuiStr rep2{};
      RolltuiSyntax* u = rolltui_syntax_new_standard(&rep2);
      check(rep2.n == 0 && lang_of(u, "Lua") >= 0 && lang_of(u, "Mine") < 0, "with no directory of a person's own, the shipped set alone, and nothing said");
      rolltui_str_free(&rep2);
      rolltui_syntax_free(u);
      ::unsetenv("ROLL_CONFIG_DIR");
      fs::remove_all(root, ec);
    }
  }
  check(live_bytes() == base, "everything loaded, run and freed: the library holds what it held before (" + std::to_string(live_bytes()) + " vs " + std::to_string(base) + ")");
  return testkit::report("rolltui_syntax_langs_test");
}
