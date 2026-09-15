//
// ownership_test.cpp — OWNERSHIP IS A STATED RULE WITH A GREP CONTROL.
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
// host, a layer is a value inside a stack. Stated as "shared_ptr webs are a no
// no"; the finding that made it cheap to adopt is that the library already
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
#include "source_scan.hpp"

using namespace rolltui_test;
using namespace testkit;
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

// Every source the library is BUILT from, which now means `c/*.h` and `c/*.c`:
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
    // The generated tables are a .h/.c pair now, so the extension filter
    // below already skips them; the name check stays as the statement of intent.
    if (rel == "unicode_tables.h" || rel == "unicode_tables.c") continue;  // generated
    const std::string ext = e.path().extension().string();
    const bool is_header = ext == ".hpp" || ext == ".h";
    const bool is_source = ext == ".cpp" || ext == ".c";
    if (headers_only ? !is_header : !(is_header || is_source)) continue;
    // A TOOL is a host, not the library, and so is an EXAMPLE — `tools/` and `examples/` are
    // the two directories that mean "not the library". The rule this skips is the LIBRARY's
    // ownership discipline; a host may
    // own its own widget's context with `new`/`delete`, which is exactly what a widget plugin's
    // `destroy` slot is for.
    if (!headers_only && (rel.rfind("tools/", 0) == 0 || rel.rfind("examples/", 0) == 0)) continue;
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
// re-record**. Counting every raw-pointer declaration meant something while the
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
        // which is a C spelling this scan never saw until the library became C:
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
  // CLAUDE.md's rule is that every allocation is a CHOICE from a closed set and never an
  // invention, and that in C the rule can be TOTAL because every allocation is an explicit
  // call. An entry point without the closed SET is not enough: two C files once invented the
  // same growing buffer EIGHT times between them, under two different growth policies, with
  // every allocation dutifully going through `rolltui_mem_*`.
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
      // rolltui_mem.c DEFINES rolltui_mem_realloc rather than calling it, which is the same
      // reason rolltui_alloc.c/h are exempted above and not a second rule.
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
  // MEASURED. A count that goes DOWN fails too, for the same reason the budget
  // does: it is either a real simplification (re-record, deliberately) or the scanner
  // having stopped seeing.
  {
    struct Row {
      const char* header;
      int pointers;
    };
    // MEASURED by this test's own scanner, which prints the table it wants, so a re-record is
    // a copy-paste and never arithmetic. Two properties of the census are enforced above
    // rather than promised here:
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
        {"rolltui.h", 132}, /* +12: `RolltuiPickerActions` carries the picker scope's twelve names as BORROWED
                              static literals, the same shape as `RolltuiMenuActions`. +1 before it: `RolltuiSetting` carries five BORROWED literals where the row it replaces
                              carried four, all of them static storage alive for the process. -1 alongside it:
                              `RolltuiSettingsReport` holds its notes in a `RolltuiStrList`, which owns them.
                              +1 before: `RolltuiThemeReport.badge_mismatches` BORROWS nothing — it is that report's
                              own growing array of owned strings, freed by `rolltui_theme_report_release` beside
                              the other three. -2 before it: `RolltuiAppProfileReport`'s two array members went with the app profile.
                              +4 before that: none — `RolltuiGapReport` stores a `RolltuiStr*` it OWNS, counted below. */
        {"c/rolltui_theme_editor.h", 0},
        {"c/rolltui_keys_editor.h", 0},
        {"c/rolltui_style.h", 0},
        {"c/rolltui_diff.h", 0},
        {"c/rolltui_json.h", 0},
        {"c/rolltui_widgets.h", 0},
        {"c/rolltui_widget_picker.h", 0}, /* an opaque handle: every pointer is a parameter, none is stored */
        {"c/rolltui_hints.h", 0},  /* likewise */
        {"c/rolltui_str.h", 0},
        {"c/rolltui_layout.h", 0},
        {"c/rolltui_widget_input.h", 0},
        {"c/rolltui_theme.h", 0},
        {"c/rolltui_undo.h", 0},
        {"c/rolltui_widget_menu.h", 0},
        {"c/rolltui_theme_analysis.h", 0},
        {"c/rolltui_presets.h", 0},
        /* `RolltuiDirList::v` — the entries array, OWNED by the list and freed by `_release`,
           which is the one raw pointer this header declares. */
        
        {"c/rolltui_md_lines.h", 5},
        {"c/rolltui_screen.h", 0},
        {"c/rolltui_render.h", 0},
        {"c/rolltui_wrap.h", 0},
        {"c/rolltui_frame_ops.h", 0},
        {"c/rolltui_layout_tree.h", 4},
        {"c/rolltui_keys.h", 0},
        {"c/rolltui_markdown.h", 0},
        {"c/rolltui_widget_kinds.h", 0},
        {"c/rolltui_theme_gen.h", 0},
        {"c/rolltui_widget_transcript.h", 0},
        {"c/rolltui_effects.h", 0},
        {"c/rolltui_bindings.h", 0},
        {"c/rolltui_alloc.h", 0},
        {"c/rolltui_context.h", 7},  /* the seven subsystems a context OWNS (the host effect STATES joined the kinds), each freed by name in `rolltui_context_free` */
        {"c/rolltui_map.h", 2},
        {"c/rolltui_document.h", 0},
        {"c/rolltui_terminal.h", 0},
        {"c/rolltui_unicode.h", 0},
        {"c/rolltui_widget_menu_tree.h", 0},
        {"c/rolltui_lifetime.h", 0},
    };
    // A continuation line of a WRAPPED declaration (`const char* name, size_t len);`) has no
    // `(` and ends in `;`, so `is_stored_pointer` alone counts it as a member. Parentheses are
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
      // The public headers are `c/*.h` plus the umbrella now; `tools/` headers
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
    // A ROW THAT RISES OWES A SENTENCE saying what the new member BORROWS or OWNS, and the
    // TOTAL is the check that a move invented or lost nothing: pointers redistributing between
    // rows while the total holds is a declaration changing headers, which is not a lifetime
    // event. Re-record WHOLE from the printed table rather than by arithmetic on a delta.
    check(total == 150, "the census counted the library's STORED borrows (" + std::to_string(total) + " in public headers)");
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


  // ---- THE GLOBALS BOUNDARY: every piece of mutable process-wide state is NAMED
  // ------------------------------------------------------------------------------------------
  // A `RolltuiContext` is a real single rolltui session, and the claim that nothing in the
  // library is global is only as true as `globals.inc`. This holds the library to that file,
  // where every mutable process-wide static is named CONTEXT (session state) or PROCESS (one
  // per process, with its reason written on the row).
  //
  // **THE POINT IS THE NEW ONE, not the count.** A list alone is a snapshot; without this check
  // the first cache someone adds is global again and nothing says so. A new static must either
  // earn a row with a reason or not exist — which is the same shape as the widget-kind table and
  // the allocation strategies: a closed set plus one explicit way to extend it.
  //
  // WHAT COUNTS AS STATE: a mutable object, or a mutable POINTER to const data
  // (`static const T* p` — the pointer moves). A `static const T x` and a
  // `static const T* const x` are constants and are not on the list. A constant that merely
  // LOOKS mutable earns no row; it gets made properly const (see `globals.inc`'s own header).
  {
    struct Row { const char* file; const char* name; const char* disposition; };
    static const Row kRecorded[] = {
#define ROLLTUI_GLOBAL(f, n, d) {f, n, #d},
#include "globals.inc"
#undef ROLLTUI_GLOBAL
    };
    const std::size_t recorded_n = sizeof kRecorded / sizeof kRecorded[0];

    // The same rule the boundary was measured with, in one pass over stripped source.
    auto statics_in = [](const std::string& text) {
      std::vector<std::string> names;
      const std::string code = strip_comments_and_literals(text);
      std::istringstream in(code);
      std::string line;
      while (std::getline(in, line)) {
        const std::size_t at = line.find_first_not_of(" \t");
        if (at == std::string::npos) continue;
        const std::string t = line.substr(at);
        if (t.rfind("static", 0) != 0) continue;
        if (t.rfind("static inline", 0) == 0) continue;
        const std::string head = t.substr(0, t.find_first_of("=;"));
        if (head.find('(') != std::string::npos) continue;  // a function
        if (t.rfind("static const", 0) == 0) {
          const std::size_t star = head.find('*');
          if (star == std::string::npos) continue;                       // a constant
          if (head.find("const", star) != std::string::npos) continue;   // a CONST pointer
        }
        // every declarator on the line: drop array sizes, take the last identifier before '='
        std::string decl = t.substr(0, t.find(';'));
        std::size_t start = 0;
        for (;;) {
          const std::size_t comma = decl.find(',', start);
          std::string part = decl.substr(start, comma - start);
          part = part.substr(0, part.find('='));
          for (;;) {
            const std::size_t ob = part.find('[');
            if (ob == std::string::npos) break;
            const std::size_t cb = part.find(']', ob);
            part.erase(ob, (cb == std::string::npos ? part.size() : cb + 1) - ob);
          }
          static const std::regex kIdent(R"([A-Za-z_][A-Za-z0-9_]*)");
          std::string last;
          for (std::sregex_iterator it(part.begin(), part.end(), kIdent), e; it != e; ++it) {
            const std::string w = it->str();
            if (w == "static" || w == "const" || w == "_Atomic" || w == "_Thread_local" || w == "struct" ||
                w == "unsigned" || w == "signed" || w == "int" || w == "char" || w == "size_t" || w == "void" ||
                w == "long" || w == "short" || w == "float" || w == "double" || w == "pthread_mutex_t" ||
                w == "termios")
              continue;
            last = w;
          }
          if (!last.empty()) names.push_back(last);
          if (comma == std::string::npos) break;
          start = comma + 1;
        }
      }
      return names;
    };

    // CONTROL FIRST, because a scanner that finds nothing would pass this whole block silently.
    {
      const std::vector<std::string> planted =
          statics_in("static int g_planted;\nstatic const char* g_ptr;\n"
                     "static const char* const kConst = \"x\";\nstatic int f(void) { return 0; }\n"
                     "/* static int g_in_a_comment; */\nconst char* s = \"static int g_in_a_literal;\";\n");
      check(planted.size() == 2 && planted[0] == "g_planted" && planted[1] == "g_ptr",
            "the globals scanner finds a mutable static and a mutable POINTER, and is not fooled by a "
            "const table, a function, a comment or a literal (" + std::to_string(planted.size()) + ")");
    }

    std::vector<std::string> found, missing, unlisted;
    for (const std::string& rel : library_sources(/*headers_only=*/false)) {
      if (rel.rfind("c/", 0) != 0 || rel.size() < 2 || rel.substr(rel.size() - 2) != ".c") continue;
      const std::string base = rel.substr(rel.rfind('/') + 1);
      for (const std::string& n : statics_in(read_file(std::string(ROLLTUI_SOURCE_DIR) + "/" + rel))) {
        found.push_back(base + ":" + n);
        bool listed = false;
        for (std::size_t i = 0; i < recorded_n; ++i)
          listed = listed || (base == kRecorded[i].file && n == kRecorded[i].name);
        if (!listed) unlisted.push_back(base + ":" + n);
      }
    }
    for (std::size_t i = 0; i < recorded_n; ++i) {
      const std::string key = std::string(kRecorded[i].file) + ":" + kRecorded[i].name;
      if (std::find(found.begin(), found.end(), key) == found.end()) missing.push_back(key);
    }
    check(unlisted.empty(),
          "every mutable process-wide static in rolltui/c/ is named in globals.inc with a disposition" +
              (unlisted.empty() ? std::string() : " — NOT LISTED: " + unlisted.front()));
    check(missing.empty(), "…and every row names a static that exists" +
                               (missing.empty() ? std::string() : " — GONE: " + missing.front()));
    int ctx = 0, proc = 0;
    for (std::size_t i = 0; i < recorded_n; ++i)
      (std::string(kRecorded[i].disposition) == "CONTEXT" ? ctx : proc)++;
    // RECORDED, and both numbers move deliberately. CONTEXT is ZERO and stays there: every
    // registry and cache the library holds belongs to a session and is released by name in
    // `rolltui_context_free`. PROCESS may only fall, or rise with a reason written on the row.
    //
    // What the 16 are: the terminal (7), the allocator counters (6), a thread's scratch and a
    // host's shutdown-hook list (2), and the keyboard protocol the TTY negotiated (1). Four
    // things that are genuinely the process's, and no registry among them.
    check(ctx == 0 && proc == 16,
          "the boundary is 0 CONTEXT + 16 PROCESS (" + std::to_string(ctx) + " + " + std::to_string(proc) + ")");
  }

  // ---- the rule is WRITTEN where a reader (and a model) will meet it -----------------
  // A convention not written where models read is not a convention (CLAUDE.md's own
  // standard). These are the two places this rule has to exist, and they are checked
  // rather than hoped.
  {
    // Two names for the same file, depending on which tree this checkout is in: CLAUDE.md when
    // this repository is mounted inside roll (as its rolltui/ submodule — roll's own working
    // agreement, one level up), README.md when it stands alone (this repository's own front
    // door, at its own root). Either is fine; neither existing is not.
    std::string top = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/../CLAUDE.md");
    // Standing alone, this repository's own root IS ROLLTUI_SOURCE_DIR (a self-referential
    // symlink is what makes "rolltui/rolltui.h" resolve there) — its README.md sits beside it,
    // not one level up, which is where a nested mount's own host document would be instead.
    if (top.empty()) top = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/../README.md");
    if (top.empty()) top = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/README.md");
    check(top.find("OWNED") != std::string::npos && top.find("BORROWED") != std::string::npos &&
              top.find("shared_ptr") != std::string::npos,
          "the top-level doc (CLAUDE.md inside roll, README.md standing alone) carries the "
          "ownership rule, where every reader meets it");
    // `rolltui/Widgets.hpp` previously; the type that does the owning is
    // `RolltuiWindows` in `c/rolltui_widgets.h` now, and the rule went with it. Repointed
    // rather than dropped: this check exists because a convention nobody meets is not one, and
    // that is as true of the C header as it was of the C++ one.
    const std::string widgets = read_file(std::string(ROLLTUI_SOURCE_DIR) + "/c/rolltui_widgets.h");
    check(widgets.find("OWNED") != std::string::npos && widgets.find("BORROWED") != std::string::npos,
          "…and c/rolltui_widgets.h states it beside the type that does the owning");
  }

  // ---- NO DOUBLE-ENCODED UTF-8 IN ANY SOURCE ----------------------------------------
  // A string read as Latin-1 and written back as UTF-8 turns one character into two or three,
  // and a terminal then draws each of them in its own cell: `\xE2\x80\xBA` becomes two visible
  // glyphs, `\xC2\xB7` becomes two, and an em dash loses both continuation bytes and survives as
  // a bare `\xC3\xA2`. The line it sits on overflows and the text after it is cut short.
  //
  // It is invisible in a diff to anyone not looking for it, and a GOLDEN FRAME RE-RECORDS IT AS
  // CORRECT, so the usual instrument confirms it instead of catching it.
  //
  // The tell is exact rather than a guess: U+00C2, U+00C3 and U+00E2 are the characters a
  // Latin-1 misread leaves in front of another non-ASCII byte, and no English source puts one
  // there on purpose. Spell a non-ASCII glyph as hex escapes and this cannot happen to it.
  {
    const auto offenders = [](const std::string& bytes) {
      std::size_t n = 0;
      for (std::size_t i = 0; i + 2 < bytes.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(bytes[i]);
        const unsigned char b = static_cast<unsigned char>(bytes[i + 1]);
        const unsigned char c = static_cast<unsigned char>(bytes[i + 2]);
        if (a != 0xC3) continue;
        if (b != 0x82 && b != 0x83 && b != 0xA2) continue;  // U+00C2, U+00C3, U+00E2
        if (c < 0x80) continue;                             // ...in front of another non-ASCII byte
        ++n;
      }
      return n;
    };

    // THE CONTROL, first: a scanner that finds nothing and a scanner that cannot see are the
    // same output. This is the real corruption, spelled as the bytes it actually was.
    check(offenders(std::string("Roles \xC3\xA2\xC2\xBA a role")) == 1,
          "the double-encoding scanner sees a planted `\xE2\x80\xBA` that was read as Latin-1");
    check(offenders(std::string("filter \xC3\x82\xC2\xB7 Enter")) == 1,
          "\xE2\x80\xA6" "and a planted `\xC2\xB7`");
    check(offenders(std::string("Roles \xE2\x80\xBA a role \xC2\xB7 fine \xE2\x80\x94 fine")) == 0,
          "\xE2\x80\xA6" "and passes correctly encoded text carrying the same three characters");

    std::size_t scanned = 0, bad = 0;
    std::string worst;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(ROLLTUI_SOURCE_DIR)) {
      const std::string p = e.path().string();
      const std::string rel = p.substr(std::string(ROLLTUI_SOURCE_DIR).size() + 1);
      if (rel.rfind("third_party/", 0) == 0 || rel.rfind("ucd/", 0) == 0) continue;
      const std::string ext = e.path().extension().string();
      if (ext != ".c" && ext != ".h" && ext != ".cpp" && ext != ".hpp" && ext != ".json") continue;
      ++scanned;
      const std::size_t n = offenders(read_file(p));
      if (n != 0) {
        bad += n;
        if (worst.empty()) worst = rel;
      }
    }
    check(scanned >= 50, "scanned the library's sources and shipped files for double-encoded UTF-8 (" +
                             std::to_string(scanned) + " files)");
    check(bad == 0, "no source or shipped file carries double-encoded UTF-8 (" + std::to_string(bad) +
                        (worst.empty() ? "" : " in " + worst) + ")");
  }

  return report("rolltui_ownership_test");
}
