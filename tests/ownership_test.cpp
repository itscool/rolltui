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

// Every source the library is BUILT from, which since Phase 17 m3 means `c/*.h` and `c/*.c`:
// the C++ binding it used to also mean was deleted with that milestone, and an enumerator
// still filtering on `.hpp`/`.cpp` matched NOTHING — this suite reported "scanned 0 files" and
// four passing checks about an empty set. A control whose subject is deleted does not fail
// loudly; it goes quiet, which is the shape this file exists to catch one level down.
//
// Tools stay in the non-headers-only pass (a tool is a host, excluded below); tests, vendored
// code and the generated Unicode tables are excluded by directory — a test may hold a
// `shared_ptr` (the hosts do, legitimately), and md4c is not ours to rule on.
std::vector<std::string> library_sources(bool headers_only) {
  std::vector<std::string> out;
  for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
    const std::string p = e.path().string();
    const std::string rel = p.substr(std::string(ROLLTUI_SOURCE_DIR).size() + 1);
    if (rel.rfind("tests/", 0) == 0 || rel.rfind("third_party/", 0) == 0 || rel.rfind("ucd/", 0) == 0 ||
        rel.rfind("presets/", 0) == 0)
      continue;
    // The generated tables are a .h/.c pair since Phase 14 m5, so the extension filter
    // below already skips them; the name check stays as the statement of intent.
    if (rel == "unicode_tables.h" || rel == "unicode_tables.c") continue;  // generated
    const std::string ext = e.path().extension().string();
    const bool is_header = ext == ".hpp" || ext == ".h";
    const bool is_source = ext == ".cpp" || ext == ".c";
    if (headers_only ? !is_header : !(is_header || is_source)) continue;
    if (!headers_only && rel.rfind("tools/", 0) == 0) continue;  // a TOOL is a host, not the library
    out.push_back(rel);
  }
  return out;
}

// Everything on the line that is not a comment: cut at `//`, and remove every `/* ... */`
// span (including an unterminated one, which runs to end of line for this purpose). Used by
// the scans that match CODE, so English inside a comment cannot trip them.
std::string strip_comments(const std::string& line) {
  std::string out = line;
  const std::size_t slashes = out.find("//");
  if (slashes != std::string::npos) out.resize(slashes);
  for (;;) {
    const std::size_t open = out.find("/*");
    if (open == std::string::npos) break;
    const std::size_t close = out.find("*/", open + 2);
    if (close == std::string::npos) {
      out.resize(open);
      break;
    }
    out.erase(open, close + 2 - open);
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

// A STORED raw pointer — a struct MEMBER — as opposed to a parameter.
//
// **THE CENSUS'S PREMISE DID NOT SURVIVE THE PORT, and this is the repair rather than a
// re-record** (Phase 17 m3). Counting every raw-pointer declaration meant something while the
// public headers were C++: a raw `T*` was unusual there, the total was 122, and each new one
// was worth a sentence. In C every parameter is a pointer — the same scan over `c/*.h` counts
// **1,139**, and a check that fires on every ordinary API addition is churn with no signal,
// which is worse than no check because it trains people to edit the number.
//
// What the census actually claimed is narrower and still true: **a pointer the library STORES
// is a lifetime question; a pointer it is merely HANDED is not.** A member ends the statement
// (`RolltuiStr* p;`); a declaration takes an argument list. That is the distinction, and it is
// checkable rather than a matter of taste — the two controls below plant one of each.
bool is_stored_pointer(const std::string& code) {
  if (!std::regex_search(code, pointer_decl())) return false;
  if (code.find('(') != std::string::npos) return false;  // a function declaration, not a member
  const std::size_t end = code.find_last_not_of(" \t");
  return end != std::string::npos && code[end] == ';';
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
        // COMMENTS ARE STRIPPED BEFORE MATCHING, not merely skipped when a line IS one. The
        // regex's own `(?!.*//)` guard covers a trailing `//` and knew nothing about `/* */`,
        // which is a C spelling this scan never saw until the library became C (Phase 17 m3):
        // `unsigned char checked ROLLTUI_DEFAULT(0); /* Toggle: the new state */` matched on
        // the English word "new". A matcher that reads prose reports defects that are not
        // there, which costs exactly as much trust as one that misses defects that are.
        const std::string code = strip_comments(line);
        // `new` inside a make_unique is the sanctioned form; `operator new` is a
        // declaration, not a use.
        if (code.find("make_unique") != std::string::npos || code.find("operator new") != std::string::npos ||
            code.find("operator delete") != std::string::npos || code.find("= delete") != std::string::npos)
          continue;
        if (std::regex_search(code, manual)) hits.push_back(rel + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(hits.empty(), "…and no hand-rolled new/delete either: OWNED means unique_ptr or a value" +
                            (hits.empty() ? "" : " — " + hits.front()));
  }

  // ---- THE C SIDE'S CLOSED SET: growth has exactly one home --------------------------
  // Phase 14 m4. CLAUDE.md's rule is that every allocation is a CHOICE from a closed set and
  // never an invention, and that in C the rule can be TOTAL because every allocation is an
  // explicit call. We had the entry point (`rolltui_mem_*`) and not the set, and by the end
  // of m3 the two ported C files had invented the same growing buffer EIGHT times with two
  // different policies — Phase 13's finding reproduced exactly, one language over.
  //
  // `rolltui_mem_realloc` is how growth is spelled, so the rule is one line: only
  // `rolltui_alloc.c` may call it. `alloc` and `free` stay available everywhere, because a
  // handle and its release are not a strategy anyone can get subtly wrong. **The point is not
  // that growth is hard — it is that eight sites is eight places to have a different policy,
  // and two of them did.**
  {
    const std::regex growth(R"(\brolltui_mem_realloc\s*\()");
    std::vector<std::string> hits;
    int scanned = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(std::string(ROLLTUI_SOURCE_DIR) + "/c")) {
      const std::string name = e.path().filename().string();
      const std::string ext = e.path().extension().string();
      if (ext != ".c" && ext != ".h") continue;
      if (name == "rolltui_alloc.c" || name == "rolltui_alloc.h") continue;  // the one home
      // rolltui_mem.c DEFINES rolltui_mem_realloc (Phase 17 m1: moved from Memory.cpp,
      // Memory.hpp's C face) rather than calling it, which is the same reason
      // rolltui_alloc.c/h are exempted above and not a second rule.
      if (name == "rolltui_mem.c") continue;
      ++scanned;
      std::istringstream in(read_file(e.path().string()));
      std::string line;
      int ln = 0;
      while (std::getline(in, line)) {
        ++ln;
        if (is_comment(line)) continue;
        if (std::regex_search(line, growth)) hits.push_back(name + ":" + std::to_string(ln) + ":" + line);
      }
    }
    check(scanned >= 5, "scanned the library's C sources (" + std::to_string(scanned) + " files)");
    check(hits.empty(), "GROWTH HAS ONE HOME: no C file outside rolltui_alloc.c grows a buffer itself" +
                            (hits.empty() ? "" : " — " + hits.front()));
    // CONTROL: the scanner finds the pattern when it is there. An absence check that has
    // stopped matching is indistinguishable from an absence — the same rule as the two above.
    check(std::regex_search(std::string("  f->cells = rolltui_mem_realloc(f->cells, n * 2);"), growth),
          "…and the pattern matches a planted growth call, so the control is live");
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
    // THE CENSUS, RE-RECORDED WHOLE 2026-09-05 (Phase 17 m3), because its subject moved: the
    // public headers are `c/*.h` now, and every `rolltui/*.hpp` row named a deleted file. Two
    // things changed with it, both enforced above rather than promised here:
    //   - it counts STORED pointers (struct members), not every declaration. Counting all of
    //     them gives 1,139 against the C++ era's 122 — in C every parameter is a pointer, so
    //     the old rule would fire on every ordinary API addition and mean nothing. What the
    //     census actually claimed is narrower and survives: a pointer the library STORES is a
    //     lifetime question; one it is merely HANDED is not.
    //   - comments are stripped before matching, because the C headers describe pointers in
    //     prose far more than the C++ ones did.
    // The per-row numbers are MEASURED (`ROLLTUI_CENSUS=1` prints this table), never guessed.
    // A row that rises still owes a sentence saying what the new member BORROWS or OWNS.
    const Row recorded[] = {
        {"rolltui.h", 118},
        {"c/rolltui_style.h", 0},
        {"c/rolltui_diff.h", 0},
        {"c/rolltui_json.h", 0},
        {"c/rolltui_widgets.h", 0},
        {"c/rolltui_str.h", 0},
        {"c/rolltui_layout.h", 0},
        {"c/rolltui_input.h", 0},
        {"c/rolltui_theme.h", 0},
        {"c/rolltui_undo.h", 0},
        {"c/rolltui_menu.h", 0},
        {"c/rolltui_theme_analysis.h", 0},
        {"c/rolltui_app_profile.h", 0},
        {"c/rolltui_presets.h", 0},
        {"c/rolltui_md_lines.h", 5},
        {"c/rolltui_screen.h", 0},
        {"c/rolltui_render.h", 0},
        {"c/rolltui_wrap.h", 0},
        {"c/rolltui_frame_ops.h", 0},
        {"c/rolltui_layout_tree.h", 0},
        {"c/rolltui_keys.h", 0},
        {"c/rolltui_markdown.h", 0},
        {"c/rolltui_widget_kinds.h", 0},
        {"c/rolltui_theme_gen.h", 0},
        {"c/rolltui_transcript.h", 0},
        {"c/rolltui_effects.h", 0},
        {"c/rolltui_bindings.h", 0},
        {"c/rolltui_alloc.h", 0},
        {"c/rolltui_map.h", 2},
        {"c/rolltui_document.h", 0},
        {"c/rolltui_terminal.h", 0},
        {"c/rolltui_unicode.h", 0},
        {"c/rolltui_menu_tree.h", 0},
        {"c/rolltui_lifetime.h", 0},
    };
    // A continuation line of a WRAPPED declaration (`const char* name, size_t len);`) has no
    // `(` and ends in `;`, so `is_stored_pointer` alone counts it as a member — found 2026-09-06
    // when a re-record moved by one for a reason the rule could not state. Parentheses are
    // tracked across lines; a line that starts inside an open one is a continuation, never a
    // member. Both halves are asserted below on literal snippets.
    auto count_stored = [](const std::string& text) {
      std::istringstream in(text);
      std::string line;
      int n = 0, depth = 0;
      while (std::getline(in, line)) {
        if (is_comment(line)) continue;
        const std::string code = strip_comments(line);
        const bool continuation = depth > 0;
        for (const char c : code) {
          if (c == '(') ++depth;
          else if (c == ')' && depth > 0) --depth;
        }
        if (continuation) continue;
        if (!is_stored_pointer(code)) continue;
        ++n;
      }
      return n;
    };
    int total = 0, checked = 0;
    std::vector<std::string> unlisted;
    for (const std::string& rel : library_sources(/*headers_only=*/true)) {
      // The public headers are `c/*.h` plus the umbrella since Phase 17 m3; `tools/` headers
      // are a HOST's and stay out. This used to read "no slash at all", which was the same set
      // back when the public headers were `rolltui/*.hpp` — and silently became the empty set
      // the moment they moved one directory down.
      if (rel.rfind("tools/", 0) == 0) continue;
      if (rel.find('/') != std::string::npos && rel.rfind("c/", 0) != 0) continue;
      // Comments stripped, not merely skipped — the C headers document pointers in prose far
      // more than the C++ ones did, and `/* a BORROW of `RolltuiStr* p` */` is not a declaration.
      const int n = count_stored(read_file(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel));
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
    // 115 -> 121 (Phase 17 m1c): +1 each for `input_handle`/`menu_handle`/`transcript_handle`
    // and +3 for `Windows`' three typed accessors handing back the library's own handles. Every
    // one is a BORROW; what left in the same change is three C++ maps that OWNED widgets.
    // 121 -> 122 (Phase 17 m3): `PresetStore.hpp`'s `handle()`, above.
    // 122 -> 199 (Phase 17 m3): a different measurement of a different set — STORED pointers
    // in `c/*.h`, where the old figure was every declaration in `rolltui/*.hpp`. Not comparable,
    // and deliberately not presented as a delta.
    // 194 -> 193 (Phase 18 m3): +1 `RolltuiPresetDomain::report`, which BORROWS a library static
    // (the domain's own report ops, set by its `_init`); -2 from two wrapped parameter-list
    // continuation lines that the scanner had been counting as members — `rolltui_preset_shipped`'s
    // and `rolltui_preset_working_value`'s second lines (`const char* name, size_t len);`) became
    // one-liners when they lost a parameter. A line with no `(` that ends in `;` is not always a
    // member: the rule above over-counts a wrapped declaration by one. Noted here rather than
    // repaired, because repairing it re-records every row; the trigger is the next re-record
    // that has to explain one of these.
    // 193 -> 194 (Phase 19 m2): the scanner's wrapped-declaration artefact again, on the new
    // `rolltui_menu_item_set` (its second line `size_t label_len, const char* shortcut, …);` has no
    // `(` and ends in `;`). Nothing new is STORED; the row rises by one for the same reason
    // `rolltui_presets.h`'s fell by two the day before. Still noted, still not repaired here.
    // RE-RECORDED WHOLE 2026-09-06 (Phase 19 m2): the public declarations moved into `rolltui.h`,
    // the definition, so its row went 0 -> 171 and every `c/` row fell to what the library keeps
    // for itself; the total is unchanged at 194, which is the check that nothing was invented
    // or lost in the move. Every one of the 171 is a BORROW or an OWNED member whose lifetime
    // the struct's own comment states, exactly as it did in the header it came from.
    // 194 -> 193 in Phase 19 m3: the one that went was the `const size_t** out` of
    // `rolltui_menu_flat_path`, a DELETE row (reached by nothing) whose declaration left
    // `c/rolltui_menu.h` with the function; the fifteen headers left with nothing (every row a 0)
    // left the table with them.
    // 125 -> 125 (Phase 20 m3): three internal headers were RE-CREATED (`render`, `wrap`,
    // `lifetime`), each declaring functions and storing no pointer, so they enter the census at 0.
    // 193 -> 125 (2026-09-06): the scanner stopped counting a wrapped declaration's continuation
    // line as a member — 68 of the 193 were `const char* name, size_t len);`-shaped second lines
    // of prototypes, 53 of them in the definition. Every row re-recorded from the printed table;
    // no pointer was added or removed.
    // 125 (Phase 20 m1-m5, after the scanner stopped counting wrapped-declaration continuation
    // lines) -> 125 with the rows redistributed (Phase 20 m6/m7): 135 declarations moved from
    // the definition into their modules' internal headers and six headers were re-created, so
    // pointers moved BETWEEN rows without any being added or removed. Re-recorded whole from the
    // printed table, which is why the total is unchanged and the rows are not.
    check(total == 125, "the census counted the library's STORED borrows (" + std::to_string(total) + " in public headers)");
    // CONTROL 3: a member counts, a wrapped declaration's continuation line does not.
    check(count_stored("struct S {\n  const char* p;\n};\n") == 1 &&
              count_stored("void f(\n    const char* name, size_t len);\n") == 0 &&
              count_stored("void g(int a,\n       RolltuiStr* out);\nstruct T { RolltuiStr* q; };\n") == 1,
          "the scanner counts a stored member and NOT a wrapped declaration's continuation line");
    // CONTROL 2: the pointer scanner actually matches a declaration, and does NOT match
    // arithmetic or a comment.
    check(std::regex_search(std::string("void f(const Document* doc);"), pointer_decl()), "the pointer scanner matches a declaration");
    check(!std::regex_search(std::string("  int n = a * b;"), pointer_decl()), "…and not a multiplication");
    // …and the STORED/HANDED split the census now turns on, planted both ways.
    check(is_stored_pointer("  RolltuiStr* notes;"), "a stored pointer is a member: it ends the statement");
    check(!is_stored_pointer("void f(const RolltuiDocument* doc);"), "…and a parameter is not one, however many it takes");
    check(!is_stored_pointer("  RolltuiStr* p = f(x);"), "…nor is a local initialised from a call");
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
    // `rolltui/Widgets.hpp` until Phase 17 m3; the type that does the owning is
    // `RolltuiWindows` in `c/rolltui_widgets.h` now, and the rule went with it. Repointed
    // rather than dropped: this check exists because a convention nobody meets is not one, and
    // that is as true of the C header as it was of the C++ one.
    const std::string widgets = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/c/rolltui_widgets.h");
    check(widgets.find("OWNED") != std::string::npos && widgets.find("BORROWED") != std::string::npos,
          "…and c/rolltui_widgets.h states it beside the type that does the owning");
  }

  return report("rolltui ownership_test");
}
