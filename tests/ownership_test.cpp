//
// ownership_test.cpp — Phase 13 m2: OWNERSHIP IS A STATED RULE WITH A GREP CONTROL.
//
// THE RULE, and it is true of `rolltui/` today rather than aspirational — which is the
// whole reason it is worth freezing:
//
//   OWNED     exactly one owner, and it is a `unique_ptr` or a value member. `Windows`
//             owns its widgets (`map<string, unique_ptr<Widget>>`); `WindowStack` owns
//             its layers BY VALUE; a `Frame` owns its cells.
//   BORROWED  every raw `T*` and every `string_view` in a public header. A borrow never
//             owns, never frees, and must not outlive the caller's frame — a bound
//             `const Document*` is the HOST's document, and the host outliving the
//             binding is the host's rule to keep.
//   VALUE     everything else. Layers, Styles, Contents, Marks: copied, not referenced.
//
// **THERE IS NO THIRD OWNERSHIP SHAPE, and in particular there is no shared one.** A
// `shared_ptr` makes a lifetime a runtime question, and every lifetime in this library is
// structural: a widget outlives every layout that names it, a document belongs to the
// host, a layer is a value inside a stack. The user put it as "shared_ptr webs are a no
// no" (2026-09-03); the finding that made it cheap to adopt is that the library already
// had none, so this file freezes a property rather than demanding a migration.
//
// TWO CONTROLS, because an ABSENCE check that has stopped working looks exactly like an
// absence:
//   1. the scanner is run over a string that DOES contain the pattern, and must find it;
//   2. the borrowed-pointer census is a recorded NUMBER per header, so a new raw pointer
//      fails the test until someone writes down what its lifetime is. That is the point:
//      the failure is not "you may not add a pointer", it is "say what it borrows".
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "rolltui_test.hpp"

using namespace rolltui_test;
namespace fs = std::filesystem;

#ifndef ROLLTUI_SOURCE_DIR
#error "ROLLTUI_SOURCE_DIR must point at rolltui/"
#endif

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Every source the library is BUILT from: its own .hpp/.cpp and its tools'. Tests,
// vendored code and the generated Unicode tables are excluded by directory — a test may
// hold a `shared_ptr` (the hosts do, legitimately), and md4c is not ours to rule on.
std::vector<std::string> library_sources(bool headers_only) {
  std::vector<std::string> out;
  for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
    const std::string p = e.path().string();
    const std::string rel = p.substr(std::string(ROLLTUI_SOURCE_DIR).size() + 1);
    if (rel.rfind("tests/", 0) == 0 || rel.rfind("third_party/", 0) == 0 || rel.rfind("ucd/", 0) == 0 ||
        rel.rfind("presets/", 0) == 0)
      continue;
    if (rel == "unicode_tables.hpp" || rel == "unicode_tables.cpp") continue;  // generated
    const std::string ext = e.path().extension().string();
    if (headers_only ? ext != ".hpp" : (ext != ".hpp" && ext != ".cpp")) continue;
    if (!headers_only && rel.rfind("tools/", 0) == 0) continue;  // a TOOL is a host, not the library
    out.push_back(rel);
  }
  return out;
}

// A raw-pointer DECLARATION: `Foo* bar`, `const Foo* bar`, `Foo** bar`. Deliberately not
// `Foo *bar` — this library never writes it that way, and a pattern that matches both
// would also match every multiplication. Comment lines are skipped by the caller.
const std::regex& pointer_decl() {
  static const std::regex r(R"(\b[A-Za-z_][A-Za-z0-9_:]*\*+\s+[a-z_][A-Za-z0-9_]*)");
  return r;
}

bool is_comment(const std::string& line) {
  const std::size_t at = line.find_first_not_of(" \t");
  if (at == std::string::npos) return true;
  return line.compare(at, 2, "//") == 0 || line.compare(at, 2, "/*") == 0 || line[at] == '*';
}

}  // namespace

int main() {
  // ---- THE RULE: no shared ownership anywhere in the library ------------------------
  {
    const std::regex shared(R"(\bstd::(shared_ptr|weak_ptr|enable_shared_from_this)\b|\bmake_shared\b)");
    std::vector<std::string> hits;
    const std::vector<std::string> files = library_sources(/*headers_only=*/false);
    for (const std::string& rel : files) {
      const std::string text = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel);
      std::istringstream in(text);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (std::regex_search(line, shared)) hits.push_back(rel + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(files.size() >= 25, "scanned the library's own sources (" + std::to_string(files.size()) + " files)");
    check(hits.empty(), "NO SHARED OWNERSHIP in rolltui: every lifetime here is structural" +
                            (hits.empty() ? "" : " — " + hits.front()));
    // CONTROL 1: the scanner finds the pattern when it is there. An absence check that has
    // silently stopped matching is indistinguishable from an absence.
    const std::string planted = "  std::shared_ptr<Widget> w = std::make_shared<Widget>();";
    check(std::regex_search(planted, shared), "…and the pattern matches a planted shared_ptr, so the control is live");
  }

  // ---- the same rule one level down: the library never news or deletes by hand -------
  {
    const std::regex manual(R"(^\s*(?!.*//).*\b(new|delete)\s+[A-Za-z_])");
    std::vector<std::string> hits;
    for (const std::string& rel : library_sources(false)) {
      const std::string text = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel);
      std::istringstream in(text);
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (is_comment(line)) continue;
        // `new` inside a make_unique is the sanctioned form; `operator new` is a
        // declaration, not a use.
        if (line.find("make_unique") != std::string::npos || line.find("operator new") != std::string::npos ||
            line.find("operator delete") != std::string::npos || line.find("= delete") != std::string::npos)
          continue;
        if (std::regex_search(line, manual)) hits.push_back(rel + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(hits.empty(), "…and no hand-rolled new/delete either: OWNED means unique_ptr or a value" +
                            (hits.empty() ? "" : " — " + hits.front()));
  }

  // ---- BORROWED: the census ----------------------------------------------------------
  // Every raw pointer in a public header is a BORROW, and this is the tripwire that keeps
  // it that way: the count per header is RECORDED, so adding one fails until an author
  // updates the number — at which point they have had to look at it and say what it
  // borrows. The failure is never "you may not add a pointer".
  //
  // MEASURED 2026-09-03. A count that goes DOWN fails too, for the same reason the budget
  // does: it is either a real simplification (re-record, deliberately) or the scanner
  // having stopped seeing.
  {
    struct Row {
      const char* header;
      int pointers;
    };
    // MEASURED 2026-09-03 by this test's own scanner (it prints the table it wants, so a
    // re-record is a copy-paste and never arithmetic). Terminal.hpp is 0 because m2 turned
    // its one hand-rolled owner into a `unique_ptr`.
    const Row recorded[] = {
        {"AppProfile.hpp", 0},   {"Bindings.hpp", 1},      {"Diff.hpp", 0},        {"Document.hpp", 0},
        {"Effects.hpp", 2},      {"Input.hpp", 0},         {"Json.hpp", 0},        {"Keys.hpp", 0},
        {"Layout.hpp", 8},       {"Markdown.hpp", 0},      {"Marker.hpp", 0},      {"Memory.hpp", 3},       {"Menu.hpp", 7},
        // Screen.hpp 1 → 4, RE-RECORDED 2026-09-04 by Phase 14 m2, which is what this row
        // is FOR: the number moved, so somebody had to say what each new pointer is.
        //   - `RolltuiFrame* p` in `Frame::Handle` — the deleter of the frame's OWNED
        //     handle. The only one here that is not a borrow, and it is a `unique_ptr`'s
        //     deleter rather than a member, which is the sanctioned shape for OWNED.
        //   - `const char* p` twice, in `glyph()` and `link()` — BORROWS from the frame,
        //     turned into a `string_view` in the same expression and never stored. The
        //     window is stated at the C header: valid until that cell is written again.
        {"Presets.hpp", 1},      {"PresetStore.hpp", 3},   {"Scratch.hpp", 4},     {"Screen.hpp", 4},
        {"Style.hpp", 0},
        {"Terminal.hpp", 0},     {"Theme.hpp", 2},         {"ThemeAnalysis.hpp", 0}, {"ThemeGen.hpp", 0},
        {"Transcript.hpp", 3},   {"Undo.hpp", 0},          {"Unicode.hpp", 1},     {"Widgets.hpp", 10},
        // Wrap.hpp 2 → 5, RE-RECORDED 2026-09-04 by Phase 14 m3, same as Screen.hpp above:
        // the number moved, so somebody had to say what each new pointer is. The two that
        // LEFT were `const Line* begin()/end()` — a line is built on read now, so the
        // iterator is an index rather than a pointer into an array that no longer exists.
        //   - `RolltuiWrapLines* p` in `WrapLines::Handle` — the deleter of the OWNED
        //     handle, a `unique_ptr`'s deleter rather than a member, which is the
        //     sanctioned shape for OWNED and the only non-borrow here.
        //   - `const char* text` and `const WrapGrapheme* graphemes` in `operator[]` —
        //     BORROWS out of the handle, turned into a `string_view` and a `span` in the
        //     same expression and never stored. The window is stated at the C header:
        //     valid until that handle is wrapped into again, reset or destroyed.
        //   - `const WrapLines* w` / `w_` in the iterator (one declaration each, the
        //     parameter and the member) — a BORROW of the container being iterated, which
        //     by construction outlives the iterator.
        //   - `RolltuiWrapLines* owned` in the private constructor — the one pointer here
        //     whose name is the whole point: it TAKES OWNERSHIP of a handle the boundary
        //     just minted, and hands it straight to the `unique_ptr`. It is private so that
        //     the only way to get one is `clone()`, which is the only place a raw handle
        //     ever exists as a value in this header.
        {"Wrap.hpp", 6},
    };
    int total = 0, checked = 0;
    std::vector<std::string> unlisted;
    for (const std::string& rel : library_sources(/*headers_only=*/true)) {
      if (rel.find('/') != std::string::npos) continue;  // tools/ headers are a host's
      int n = 0;
      const std::string text = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel);
      std::istringstream in(text);
      std::string line;
      while (std::getline(in, line)) {
        if (is_comment(line)) continue;
        if (std::regex_search(line, pointer_decl())) ++n;
      }
      total += n;
      // The table above is PRINTED on a failure run, so re-recording is a copy-paste:
      // set ROLLTUI_CENSUS=1 to see it.
      if (std::getenv("ROLLTUI_CENSUS")) std::printf("        {\"%s\", %d},\n", rel.c_str(), n);
      const Row* row = nullptr;
      for (const Row& r : recorded)
        if (rel == r.header) row = &r;
      if (!row) {
        unlisted.push_back(rel + " (" + std::to_string(n) + ")");
        continue;
      }
      ++checked;
      check_quiet(n == row->pointers, std::string("borrowed-pointer census: ") + rel + " has " + std::to_string(n) +
                                          " raw pointers, recorded " + std::to_string(row->pointers) +
                                          " — if that is deliberate, say what the new one BORROWS and update the row");
    }
    check(unlisted.empty(), "every public header is in the census" + (unlisted.empty() ? "" : " — missing: " + unlisted.front()));
    check(checked == static_cast<int>(sizeof(recorded) / sizeof(recorded[0])),
          "…and every recorded row matched a real header (" + std::to_string(checked) + ")");
    check(total == 55, "the census counted the library's borrows (" + std::to_string(total) + " raw pointers in public headers)");
    // CONTROL 2: the pointer scanner actually matches a declaration, and does NOT match
    // arithmetic or a comment.
    check(std::regex_search(std::string("void f(const Document* doc);"), pointer_decl()), "the pointer scanner matches a declaration");
    check(!std::regex_search(std::string("  int n = a * b;"), pointer_decl()), "…and not a multiplication");
    check(is_comment("  // const Document* doc"), "…and comment lines are skipped, so prose about a pointer is not a pointer");
  }

  // ---- the rule is WRITTEN where a reader (and a model) will meet it -----------------
  // A convention not written where models read is not a convention (CLAUDE.md's own
  // standard). These are the two places this rule has to exist, and they are checked
  // rather than hoped.
  {
    const std::string claude = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/../CLAUDE.md");
    check(claude.find("OWNED") != std::string::npos && claude.find("BORROWED") != std::string::npos &&
              claude.find("shared_ptr") != std::string::npos,
          "CLAUDE.md carries the ownership rule, where every session reads it");
    const std::string widgets = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/Widgets.hpp");
    check(widgets.find("OWNED") != std::string::npos && widgets.find("BORROWED") != std::string::npos,
          "…and Widgets.hpp states it beside the type that does the owning");
  }

  return report("rolltui ownership_test");
}
