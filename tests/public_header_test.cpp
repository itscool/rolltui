// rolltui/tests/public_header_test.cpp — the guarantees `rolltui/rolltui.h` makes about
// itself, as assertions rather than as a promise in its own comment.
//
// The header is THE DEFINITION of the public API, and the guarantees are:
//   1. It COMPILES ALONE and gives you the API — this translation unit includes it and
//      nothing else of the library's, and calls it.
//   2. It DECLARES the public API and includes no `rolltui/c/` header: every PUBLIC and
//      TOOL_FACING function of api_classes.inc is declared in it and no INTERNAL one is
//      (section 6), the headers under c/ include it first and are included by no host or tool
//      (section 3, an exact zero over an explicit opt-in list of tests), and no C++ member
//      anywhere names a std:: container or view (section 7).
// It used to certify the opposite — a facade that "declares NOTHING of its own" — which is
// the mis-capture the plan records.
//
// The grep control proves itself against a planted line before it is trusted, because
// CLAUDE.md records `strings | grep MARKER` false-negativing twice in this repo and a
// scanner that cannot see a violation is worse than no scanner.
#include <cstdio>
#include <fstream>
#include <cctype>
#include <sstream>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <regex>
#include <set>
#include <vector>

#include <dirent.h>

#include "rolltui/rolltui.h"  // MUST be sufficient on its own — that is assertion 1.

#include "source_scan.hpp"  // the two strippers, shared with ownership_test

#include "rolltui_test.hpp"

using namespace rolltui_test;

namespace {

std::string read(const std::string& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// A line that is code rather than an include, a comment, the guard or blank.
bool is_declaration(const std::string& raw) {
  std::string s = raw;
  const size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return false;
  s = s.substr(a);
  if (s.rfind("//", 0) == 0 || s.rfind("/*", 0) == 0 || s.rfind("*", 0) == 0) return false;
  if (s.rfind("#include", 0) == 0) return false;
  if (s.rfind("#ifndef", 0) == 0 || s.rfind("#define ROLLTUI_H", 0) == 0 || s.rfind("#endif", 0) == 0)
    return false;
  return true;
}


void list_files(const std::string& dir, const std::vector<std::string>& exts, std::vector<std::string>& out) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  while (dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    const std::string path = dir + "/" + name;
    if (e->d_type == DT_DIR) { list_files(path, exts, out); continue; }
    for (const std::string& x : exts)
      if (name.size() > x.size() && name.compare(name.size() - x.size(), x.size(), x) == 0) out.push_back(path);
  }
  closedir(d);
}

// Every `rolltui_*` identifier in `text` (comments already stripped), counted.
void count_idents(const std::string& text, std::map<std::string, int>& out) {
  static const std::regex re(R"(\brolltui_[a-z0-9_]+\b)");
  for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) ++out[it->str()];
}
// A function DEFINITION in a .c file: a line starting at column 0 with a type, the name, a
// parameter list and an opening brace — the one mention of a function that is not a use of it.
std::set<std::string> definitions_in(const std::string& text) {
  static const std::regex re(R"((?:^|\n)[A-Za-z_][^\n;{}]*?\b(rolltui_[a-z0-9_]+)\s*\([^;{}]*\)\s*\{)");
  std::set<std::string> out;
  for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) out.insert((*it)[1].str());
  return out;
}

std::vector<std::string> declarations(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
    if (is_declaration(line)) out.push_back(line);
  return out;
}

}  // namespace

int main() {
  const std::string path = std::string(ROLLTUI_SOURCE_DIR) + "/rolltui.h";
  const std::string text = read(path);
  check(!text.empty(), "rolltui.h is where the public header is expected to be");

  // ---- 1. it is SUFFICIENT ------------------------------------------------------------
  // Proved by this translation unit: it includes rolltui.h and nothing else from the
  // library, and the calls below are what a real consumer's first five minutes look like.
  //
  // THE SUFFICIENCY CHECK MAY NOT LEAN ON ITSELF. A function kept PUBLIC because THIS TEST
  // reaches it is public for a meta-test's sake, which section 5 forbids by name — so the
  // check exercises what a host actually does: a host never builds a frame, it gets one
  // from the double buffer, and text comes back through `RolltuiStr`'s own public fields.
  RolltuiSwap* s = rolltui_swap_new(4, 2, RolltuiStyle{});
  check(s != nullptr, "the double buffer, from the umbrella header alone");
  RolltuiFrame* f = rolltui_swap_begin(s, 4, 2, RolltuiStyle{});
  check(f != nullptr, "…lends a back frame, which is how a host gets one");
  RolltuiStr out{};
  rolltui_frame_to_text(f, &out);
  check(out.p != nullptr && out.n > 0, "…and rendering it to text, which needs three headers working together");
  rolltui_str_free(&out);
  rolltui_swap_free(s);

  // ---- 2. it DECLARES the public API ----------------------------------------
  // THE DEFINITION CARRIES THE DECLARATIONS. It is not a facade that forwards to internal
  // headers: every PUBLIC function of api_classes.inc is declared HERE and no INTERNAL one
  // is (section 6), and it includes no `rolltui/c/` header at all.
  const std::vector<std::string> decls = declarations(text);
  check(decls.size() > 1500, "rolltui.h DECLARES the API: " + std::to_string(decls.size()) + " declaration lines of its own");
  check(text.find("#include \"rolltui/c/") == std::string::npos, "…and includes no rolltui/c/ header — the definition is the file, not a list");
  check(declarations("#include \"x.h\"\n// a comment\nint rolltui_planted(void);\n").size() == 1,
        "…and the scanner catches a planted declaration, so the count above means something");

  // ---- 3. the headers under c/ are the library's own ---------------------------------------
  // Enumerated from the directory. Each includes the definition FIRST (one definition per
  // type holds by construction), and none is included by a host or a tool. A TEST may include
  // one — by naming it, with the comment the opt-in carries, and by being listed in
  // rolltui/CMakeLists.txt's ROLLTUI_INTERNAL_OPT_IN. The count outside that list is 0, asserted.
  std::vector<std::string> headers;
  if (DIR* d = opendir((std::string(ROLLTUI_SOURCE_DIR) + "/c").c_str())) {
    while (dirent* e = readdir(d)) {
      const std::string n = e->d_name;
      if (n.size() > 2 && n.compare(n.size() - 2, 2, ".h") == 0) headers.push_back(n);
    }
    closedir(d);
  }
  std::sort(headers.begin(), headers.end());
  // A HEADER EXISTS BECAUSE A `.c` NEEDS A DECLARATION FROM IT. One that declares nothing
  // is a file with no reason to be — the hollow-header check below enforces that — and a
  // module whose steps move out of the definition earns a header back by the same rule.
  // The count is recorded so that a header appearing or vanishing is a deliberate act.
  const std::size_t kInternalHeaders = 35; /* +rolltui_keys_editor.h; +rolltui_theme_editor.h; +rolltui_context.h; -rolltui_app_profile.h */
  check(headers.size() == kInternalHeaders, "the internal header directory holds the recorded " + std::to_string(kInternalHeaders) + " headers [" + std::to_string(headers.size()) + "]");
  {
    std::vector<std::string> hollow;
    for (const std::string& h : headers) {
      const std::string t = strip_all_comments(read(std::string(ROLLTUI_SOURCE_DIR) + "/c/" + h));
      std::istringstream in(t);
      std::string line;
      bool content = false;
      while (std::getline(in, line)) {
        const std::size_t at = line.find_first_not_of(" \t");
        if (at == std::string::npos) continue;
        const std::string body = line.substr(at);
        if (body[0] == '#' || body == "extern \"C\" {" || body == "}") continue;
        content = true;
        break;
      }
      if (!content) hollow.push_back(h);
    }
    std::string joined;
    for (const std::string& h : hollow) joined += " " + h;
    check(hollow.empty(), "no internal header is left with nothing — a header that declares nothing is a file with no reason" + joined);
  }
  // ---- NOTHING IS DECLARED TWICE IN ONE HEADER ------------------------
  // A REPEATED DECLARATION IS LEGAL C AND THE COMPILER SAYS NOTHING, which is how six
  // headers came to declare 86 functions twice — one of them a 33-line block appearing
  // verbatim under two identical banners — with nothing failing anywhere. That is the shape
  // this repo keeps meeting: no error, no warning, and a vocabulary written down twice
  // quietly becomes a second thing to drift.
  //
  // ONE SHAPE IS LEGITIMATE: a FORWARD declaration that a C++ inline member below it has to
  // call, which the file then declares again in its ordinary section. `rolltui.h` carries a
  // literal banner over each such block — "forward declarations the C++ members just below
  // call" — and `rolltui_layout_tree.h` puts its block inside `#ifdef __cplusplus`. Those two
  // are RECORDED per header rather than pattern-matched, because a number that has to be
  // re-recorded makes a new duplicate an argument someone has to win; a pattern would just
  // absorb it. Every OTHER header must have none, and that is the ratchet.
  {
    const auto dupes_in = [](const std::string& text) {
      std::map<std::string, int> seen;
      std::istringstream in(text);
      std::string line;
      while (std::getline(in, line)) {
        const std::size_t semi = line.find_last_not_of(" \t\r");
        if (semi == std::string::npos || (line[semi] != ';' && line[semi] != ',')) continue;
        const std::size_t open = line.find('(');
        if (open == std::string::npos) continue;
        std::size_t b = open;
        while (b > 0 && (std::isalnum((unsigned char)line[b - 1]) || line[b - 1] == '_')) --b;
        const std::string name = line.substr(b, open - b);
        if (name.rfind("rolltui_", 0) != 0) continue;
        // A declaration, not a call: what precedes the name must be a return type.
        const std::size_t before = line.find_first_not_of(" \t");
        if (before == std::string::npos || before >= b) continue;
        ++seen[name];
      }
      int n = 0;
      for (const auto& kv : seen)
        if (kv.second > 1) ++n;
      return n;
    };
    // RECORDED: header -> how many functions it declares twice, and the only accepted reason
    // is the forward-declaration-for-a-C++-member shape above.
    struct Row { const char* header; int dupes; };
    static const Row kRows[] = {{"rolltui.h", 19}, {"c/rolltui_layout_tree.h", 21}};
    int accounted = 0;
    for (const Row& r : kRows) {
      const int got = dupes_in(depth0(strip_all_comments(read(std::string(ROLLTUI_SOURCE_DIR) + "/" + r.header))));
      check(got == r.dupes, std::string("forward declarations in ") + r.header + " are the recorded " +
                                std::to_string(r.dupes) + " [" + std::to_string(got) + "]");
      accounted += got;
    }
    std::vector<std::string> offenders;
    for (const std::string& h : headers) {
      if (std::string("c/" + h) == "c/rolltui_layout_tree.h") continue;
      const int got = dupes_in(depth0(strip_all_comments(read(std::string(ROLLTUI_SOURCE_DIR) + "/c/" + h))));
      if (got) offenders.push_back(h + " (" + std::to_string(got) + ")");
    }
    std::string names;
    for (const std::string& o : offenders) names += " " + o;
    check(offenders.empty(),
          "no other header declares anything twice — a repeated declaration compiles silently and is a "
          "vocabulary written down twice" + names);
    check(accounted == 40, "…and the accepted forward declarations are the recorded 40 [" + std::to_string(accounted) + "]");
  }
  {
    std::vector<std::string> without;
    for (const std::string& h : headers) {
      const std::string t = read(std::string(ROLLTUI_SOURCE_DIR) + "/c/" + h);
      if (h == "rolltui_alloc.h" || h == "rolltui_map.h") continue;  // the two that predate the definition and include what they need
      const std::size_t first = t.find("#include ");
      const std::string def = "#include \"rolltui/rolltui.h\"";
      if (first == std::string::npos || t.compare(first, def.size(), def) != 0) without.push_back(h);
    }
    std::string joined;
    for (const std::string& w : without) joined += " " + w;
    check(without.empty(), "every internal header includes the definition FIRST, so a type is defined once" + joined);
  }
  {
    std::set<std::string> optin;
    {
      std::string list = ROLLTUI_INTERNAL_OPT_IN;
      std::size_t at = 0;
      while (at <= list.size()) {
        const std::size_t comma = list.find(',', at);  // comma-joined: a ';' would be a shell separator in the compile line
        optin.insert(list.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        if (comma == std::string::npos) break;
        at = comma + 1;
      }
    }
    const std::string repo = std::string(ROLLTUI_SOURCE_DIR) + "/..";
    std::vector<std::string> files;
    list_files(repo + "/src", {".cpp", ".hpp"}, files);
    list_files(repo + "/include", {".hpp"}, files);
    list_files(repo + "/tests", {".cpp", ".hpp"}, files);
    list_files(repo + "/tools", {".cpp", ".hpp"}, files);
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/tools", {".cpp", ".hpp"}, files);
    // `rolltui/examples/` is scanned too, and NOTHING in it may opt in. It holds the
    // two CONSUMERS — `rolltui-paint` (moved here from `tools/` when the directory's name
    // finally misled someone) and `rolltui-explorer` — and a consumer that reaches past the
    // definition is what makes this zero worth asserting.
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/examples", {".cpp", ".hpp"}, files);
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/tests", {".cpp", ".hpp", ".c"}, files);
    std::vector<std::string> offenders;
    int opted = 0;
    for (const std::string& f : files) {
      if (strip_all_comments(read(f)).find("#include \"rolltui/c/") == std::string::npos) continue;
      const std::string base = f.substr(f.find_last_of('/') + 1);
      // the opt-in list is no longer only tests. The studio and its three editors
      // are on it — rolltui's OWN authoring tool, which is removed from the
      // consumer set — so a listed file may live under `tests/` OR under `rolltui/tools/`.
      // `rolltui-paint` is deliberately NOT on the list: it is a consumer and must keep
      // building from the definition alone, which is what makes this zero mean something.
      // `tools/bench` joined the two rolltui directories. The opt-in LIST is the
      // authority on who may reach an internal header; this bound only says where such a file
      // may live, and rolltui's benches live at the repo root beside its other instruments.
      const bool listed_dir = f.find(std::string(ROLLTUI_SOURCE_DIR) + "/tests/") != std::string::npos ||
                              f.find(std::string(ROLLTUI_SOURCE_DIR) + "/tools/") != std::string::npos ||
                              f.find("/tools/bench/") != std::string::npos;
      if (listed_dir && optin.count(base)) { ++opted; continue; }
      offenders.push_back(f.substr(f.find("/tui/") == std::string::npos ? 0 : f.find("/tui/") + 5));
    }
    std::string joined;
    for (const std::string& o : offenders) joined += "\n      " + o;
    check(opted >= 5, "the include scanner sees the opted-in tests (" + std::to_string(opted) + " of " + std::to_string(optin.size()) + " listed)");
    check(offenders.empty(), "no host, tool or unlisted test includes a rolltui/c/ header — the count is 0, not a ratchet" + joined);
  }

  // ---- 4. the THREE TEXT-OUT SHAPES, and (a) carries a checkable promise --------------
  // rule 3 says a fixed-buffer function is used only where the maximum is KNOWN and NAMED, and
  // the cap has to be in the DEFINITION so a caller can size the buffer.
  {
    const char* kFixedBuffer[] = {"rolltui_chord_display",   "rolltui_chord_to_string",
                                  "rolltui_color_to_string", "rolltui_dim_to_string",
                                  "rolltui_md_decode_entity", "rolltui_scroll_marker_text",
                                  "rolltui_sgr",             "rolltui_split_size_to_string"};
    int unsized = 0;
    std::string names;
    for (const char* f : kFixedBuffer) {
      if (text.find(f) == std::string::npos) continue;  // internal now, or gone; not this test's claim
      if (text.find("_MAX") == std::string::npos) { ++unsized; names += std::string(" ") + f; }
    }
    check(unsized == 0, "every fixed-buffer text function is sized by a named ROLLTUI_*_MAX" + (unsized ? names : std::string(" (8 checked)")));
    check(text.find("ROLLTUI_SGR_MAX") != std::string::npos && text.find("ROLLTUI_CHORD_STRING_MAX") != std::string::npos,
          "…and the caps are in the DEFINITION, so a caller can actually declare the buffer");
  }

  // ---- 5. (the .hpp ratchet, retired) --------------------------------------------------
  // It counted consumers including a C++ `rolltui/*.hpp`; none exist now, and
  // section 3 above counts the thing that matters now — direct `rolltui/c/` includes — as an
  // exact zero rather than a ceiling.

  // ---- NO PUBLIC FUNCTION HANDS A RESULT BACK THROUGH A CALLBACK -------------------------
  // A callback parameter is legitimate when it carries a DECISION the library cannot make; it
  // is wrong when it carries a RESULT the library already has, because then every consumer
  // writes the same lambda-and-collector (`rolltui_preset_store_list` was wrapped at 7 of 7
  // sites before this check existed). Scanned over the DEFINITION's declarations.
  {
    static const char* kResultSinks[] = {"RolltuiPutFn", "RolltuiPresetInfoFn", "RolltuiMenuActionFn"};
    static const char* kAllowed[] = {"rolltui_preset_read_file"};
    std::vector<std::string> offenders;
    int scanned = 0, sentinel = 0;
    std::string stmt;
    for (const std::string& line : decls) {
      stmt += (stmt.empty() ? "" : " ") + line;
      if (line.find(';') == std::string::npos) continue;
      const std::string decl = stmt;
      stmt.clear();
      if (decl.find("rolltui_") == std::string::npos || decl.find('(') == std::string::npos) continue;
      ++scanned;
      ++sentinel;
      if (decl.rfind("typedef", 0) == 0) continue;
      bool sink = false;
      for (const char* t : kResultSinks) sink = sink || decl.find(t) != std::string::npos;
      if (!sink) continue;
      bool allowed = false;
      for (const char* a : kAllowed) allowed = allowed || decl.find(a) != std::string::npos;
      if (!allowed) offenders.push_back(decl.substr(0, 90));
    }
    check(sentinel > 200, "the declaration scanner can see: " + std::to_string(sentinel) + " of " + std::to_string(scanned) + " scanned declarations name a rolltui_ symbol");
    std::string joined;
    for (const std::string& o : offenders) joined += "\n      " + o;
    check(offenders.empty(), "no public function hands a RESULT back through a callback — it takes the caller's RolltuiStr* (one string), RolltuiStrList* (many) or typed list instead" + joined);
  }

  // ---- 6. THE CLASS TABLE: every function has ONE class, and the definition is written from
  //         it ---------------------------------------------------------------------------
  // Reach is MEASURED — who outside the library mentions each function — and the table is held
  // to it; then the DEFINITION is held to the table: a PUBLIC or TOOL_FACING row is declared in
  // rolltui.h and not in a c/ header, an INTERNAL or DELETE row the other way round.
  {
    struct Row { const char* fn; const char* cls; };
    static const Row kApi[] = {
#define ROLLTUI_API(name, cls) {#name, #cls},
#include "api_classes.inc"
#undef ROLLTUI_API
    };
    const std::string root = std::string(ROLLTUI_SOURCE_DIR);
    const std::string repo = root + "/..";
    static const std::regex decl_re(R"(\b(rolltui_[a-z0-9_]+)\s*\()");
    // DECLARED means at brace depth zero: a C++ member of a public struct that CALLS
    // `rolltui_str_append` mentions it without declaring it, and the first draft of this check
    // counted the mention — a planted removal of the declaration itself passed. The braces of
    // `extern "C" {` and `namespace x {` are not depth; everything else's are.
    auto names_in = [&](const std::string& t, std::set<std::string>& into) {
      for (std::sregex_iterator it(t.begin(), t.end(), decl_re), end; it != end; ++it) into.insert((*it)[1].str());
    };
    std::set<std::string> in_def, in_internal, mentioned_in_def;
    names_in(depth0(strip_all_comments(text)), in_def);
    names_in(strip_all_comments(text), mentioned_in_def);
    for (const std::string& h : headers) names_in(depth0(strip_all_comments(read(root + "/c/" + h))), in_internal);
    check(depth0("void a(void);\nstruct S {\n  void m() { b(); }\n};\nextern \"C\" {\nvoid c(void);\n}\n").find("b()") == std::string::npos &&
              depth0("extern \"C\" {\nvoid c(void);\n}\n").find("c(void)") != std::string::npos,
          "the depth-zero filter drops a member body and keeps a declaration under extern \"C\"");
    // THE STRIPPER, PROVED ON THE THREE SHAPES THAT BROKE IT. Each of these made
    // the census UNDER-report a consumer's reach, which is a wrong class rather than a loud
    // failure — the reports-zero shape aimed at the instrument the whole table is held to.
    check(strip_comments_and_literals("const char* k = \"//status\";\nrolltui_kept_after_a_slash_slash_string();\n")
                  .find("rolltui_kept_after_a_slash_slash_string") != std::string::npos &&
              strip_comments_and_literals("/* don't */\nrolltui_kept_after_an_apostrophe_in_prose();\n")
                  .find("rolltui_kept_after_an_apostrophe_in_prose") != std::string::npos &&
              strip_comments_and_literals("#error \"a \\\n continuation\"\nrolltui_kept_after_a_continued_literal();\n")
                  .find("rolltui_kept_after_a_continued_literal") != std::string::npos &&
              strip_comments_and_literals("rolltui_gone_inside(\"rolltui_gone_inside_a_literal\");\n")
                  .find("rolltui_gone_inside_a_literal") == std::string::npos,
          "the census stripper keeps code after a \"//\"-string, after an apostrophe in prose and after a continued literal, and still drops what is inside a literal");
    std::set<std::string> declared;
    declared.insert(in_def.begin(), in_def.end());
    declared.insert(in_internal.begin(), in_internal.end());
    auto mentions_in = [&](const std::vector<std::string>& dirs, const std::vector<std::string>& exts) {
      std::vector<std::string> files;
      for (const std::string& d : dirs) list_files(d, exts, files);
      std::map<std::string, int> counts;
      for (const std::string& f : files) count_idents(strip_comments_and_literals(read(f)), counts);
      std::set<std::string> out;
      for (const auto& [k, v] : counts) out.insert(k);
      return out;
    };
    // `roll` gains its own bench tools — they build frames the way a host does.
    // `tools/bench` LEFT this set. A bench is rolltui's own INSTRUMENT, not roll the
    // consumer — `frame_allocs.cpp` builds a bare frame on purpose, because the number it reports
    // is that frame's own allocation and `rolltui_swap_begin` would measure the double buffer's
    // reuse instead. Counting it as roll made `rolltui_frame_new`/`_free` look consumer-reached
    // and would have held them public for an instrument's sake, which is the same shape as
    // holding one public for a test's sake. It is on the opt-in list instead.
    const std::set<std::string> roll = mentions_in({repo + "/src", repo + "/include"}, {".cpp", ".hpp"});
    // THE STUDIO IS NOT A CONSUMER, by decision: it and its three editors are rolltui's OWN
    // authoring tool for rolltui's OWN files, nobody outside this repo builds one, and it
    // opts in to internal headers like a suite.
    //
    // THE CONSUMER SET IS `rolltui/examples/`, and the two examples have different jobs.
    // `paint` is the ADVERSARIAL probe — a generic painting app against the grain of every
    // vocabulary the library models, so a limit it hits may be correct and is reported
    // rather than fixed. The `explorer` is the ALIGNED probe — a Miller-column browser is
    // what a terminal UI library is FOR, and it is the first widget in the tree with
    // internal structure the library does not model, so what it reaches is the floor the
    // public surface cannot go below.
    std::set<std::string> paint_reach;
    {
      std::map<std::string, int> counts;
      for (const char* f : {"/examples/paint.cpp", "/examples/explorer.cpp", "/tools/tool_str.hpp"})
        count_idents(strip_comments_and_literals(read(root + f)), counts);
      for (const auto& [k, v] : counts) paint_reach.insert(k);
    }
    const std::set<std::string> tools = paint_reach;
    const std::set<std::string> tests = mentions_in({root + "/tests", repo + "/tests"}, {".cpp", ".hpp", ".c"});
    // the PUBLIC-ONLY suites — programs shaped like a CONSUMER, which include
    // `rolltui/rolltui.h` and nothing else. What one of them reaches is PUBLIC because a
    // consumer-shaped program reaches it, NOT because a test does; a test's reach is never a
    // reason (see api_classes.inc's header). roll's own tests are all in the set implicitly:
    // roll is a host, and its tests build the way roll builds.
    std::set<std::string> public_only_files;
    {
      std::string list = ROLLTUI_PUBLIC_ONLY_TESTS;
      std::size_t at = 0;
      while (at <= list.size()) {
        const std::size_t comma = list.find(',', at);
        public_only_files.insert(list.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        if (comma == std::string::npos) break;
        at = comma + 1;
      }
    }
    std::set<std::string> public_only;
    {
      std::vector<std::string> files;
      list_files(root + "/tests", {".cpp", ".hpp", ".c"}, files);
      std::vector<std::string> roll_tests;
      list_files(repo + "/tests", {".cpp", ".hpp"}, roll_tests);
      for (const std::string& f : roll_tests) files.push_back(f);
      std::map<std::string, int> counts;
      for (const std::string& f : files) {
        const std::string base = f.substr(f.find_last_of('/') + 1);
        const bool rolls = f.find("/rolltui/tests/") == std::string::npos;
        if (!rolls && !public_only_files.count(base)) continue;
        count_idents(strip_comments_and_literals(read(f)), counts);
      }
      for (const auto& [k, v] : counts) public_only.insert(k);
    }
    std::set<std::string> lib;
    {
      std::vector<std::string> cs;
      list_files(root + "/c", {".c"}, cs);
      for (const std::string& f : cs) {
        const std::string t = strip_comments_and_literals(read(f));
        const std::set<std::string> defs = definitions_in(t);
        std::map<std::string, int> counts;
        count_idents(t, counts);
        for (const auto& [k, v] : counts)
          if (defs.count(k) ? v > 1 : true) lib.insert(k);
      }
    }
    auto reach_of = [&](const std::string& f) -> const char* {
      return roll.count(f) ? "roll" : tools.count(f) ? "tools" : tests.count(f) ? "tests" : lib.count(f) ? "lib" : mentioned_in_def.count(f) ? "hdr" : "nothing";
    };
    /* The floors say the census PARSED something, never what the surface should be. A floor
     * that has to be edited every time the surface shrinks is measuring the wrong thing;
     * this one is armed at any plausible size and dead only if the parser returns
     * nothing. */
    check(declared.size() > 700 && roll.count("rolltui_preset_store_new") && tools.count("rolltui_context_register_kind") /* paint registers its canvas kind */ &&
              lib.count("rolltui_str_append") && !lib.count("rolltui_preset_store_new_NOSUCH") && in_def.size() > 250 && in_internal.size() > 400,
          "the class census sees the definition (" + std::to_string(in_def.size()) + " named), the internal headers (" + std::to_string(in_internal.size()) + "), roll's reach, the tools' reach and the library's own");
    std::map<std::string, std::string> cls;
    for (const Row& r : kApi) cls[r.fn] = r.cls;
    // A row PUBLIC for a stated reason rather than for a consumer's reach carries `KEPT: <why>`
    // in the table itself, so the exception is machine-readable and countable — writing one is a
    // DECISION a reader can audit, not a comment nobody re-reads. Their number is recorded.
    std::set<std::string> kept;
    {
      const std::string tbl = read(root + "/tests/api_classes.inc");
      static const std::regex kept_re(R"(ROLLTUI_API\(\s*(rolltui_[a-z0-9_]+)\s*,[^)]*\)\s*/\*\s*KEPT:)");
      for (std::sregex_iterator it(tbl.begin(), tbl.end(), kept_re), end; it != end; ++it) kept.insert((*it)[1].str());
    }
    // ---- THE NOT-OPTED-IN GUARD -------------------------------------------------------------
    // Moving a function INTERNAL costs a suite an opt-in, so `ROLLTUI_INTERNAL_OPT_IN` only grows;
    // if nearly every suite ends up on it, section 3's exact zero stops saying anything. **The
    // instrument is the set that stays OUT.** These four assertions are what keep it meaningful:
    // the set is non-empty, every name in it is a real file (a typo would shrink it silently —
    // the reports-zero shape aimed at this very check), the pure-C consumer is in it, and at
    // least one whole-host suite is. Both lists are printed under ROLLTUI_CENSUS so a re-record
    // is a copy-paste rather than arithmetic.
    {
      auto join = [](const std::vector<std::string>& v) { std::string t; for (const std::string& x : v) t += "\n      " + x; return t; };
      std::vector<std::string> missing;
      for (const std::string& n : public_only_files)
        if (!n.empty() && read(root + "/tests/" + n).empty()) missing.push_back(n);
      std::set<std::string> optin_now;
      {
        std::string list = ROLLTUI_INTERNAL_OPT_IN;
        std::size_t at = 0;
        while (at <= list.size()) {
          const std::size_t comma = list.find(',', at);
          optin_now.insert(list.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
          if (comma == std::string::npos) break;
          at = comma + 1;
        }
      }
      std::vector<std::string> both;
      for (const std::string& n : public_only_files)
        if (optin_now.count(n)) both.push_back(n);
      static const char* kWholeHost[] = {"authored_screen_test.cpp", "files_only_test.cpp", "studio_golden_test.cpp"};
      int whole_host = 0;
      for (const char* n : kWholeHost) whole_host += public_only_files.count(n) ? 1 : 0;
      check(!public_only_files.empty() && public_only_files.size() >= 6,
            "THE PUBLIC-ONLY SET IS NON-EMPTY (" + std::to_string(public_only_files.size()) +
                ") — it is the instrument, not the opt-in list beside it");
      check(missing.empty(), "every name in ROLLTUI_PUBLIC_ONLY_TESTS is a real file — a typo would shrink the set in silence" + join(missing));
      check(public_only_files.count("c_consumer_test.c") == 1,
            "the pure-C consumer is in the public-only set, and can never leave it: it is the proof that the public API alone builds a screen, draws a frame and drives a preset store");
      check(whole_host >= 1, "at least one WHOLE-HOST suite is public-only, so the set covers a real app's path (" + std::to_string(whole_host) + " of 3)");
      check(both.empty(), "no suite is in both lists — a suite either reaches internals or proves the public API is enough" + join(both));
      if (std::getenv("ROLLTUI_CENSUS")) {
        std::printf("        PUBLIC-ONLY (%zu):", public_only_files.size());
        for (const std::string& n : public_only_files) std::printf(" %s", n.c_str());
        std::printf("\n        OPTED IN (%zu):", optin_now.size());
        for (const std::string& n : optin_now) std::printf(" %s", n.c_str());
        std::printf("\n");
      }
    }
    // 15 -> 57. The new ones are not new exceptions, they are the SAME rule
    // written down where it applies: 37 functions a public C++ member in the definition calls
    // (found by COMPILING the definition — no reach bucket can see a member body, which is why
    // the loop that found them iterated until it compiled), four the sufficiency check in
    // section 1 needs, and `rolltui_rect_intersect`, whose declaration never moved so the
    // compile loop could not flag it. Each carries its sentence in the table.
    // 57 -> 59: the two colour functions above, each carrying its KEPT reason.
    // 70 -> 65: five went with the app profile. Each had been kept as the MATCHING
    // PART of a pair — an accessor whose sibling a consumer reached, so that "a profile you can
    // parse but whose actions you cannot read is a hole". The pairs are whole because the module
    // is gone, which is the cheapest way a kept row ever resolves.
    // 65 -> 66: the `theme` widget kind's one, which is how an app says where its theme lives.
    // 66 -> 67: the `keys` widget kind's, for the same reason one rung over — where a rebinding lands.
    check(kept.size() == 67,
          "the KEPT rows — PUBLIC for a stated reason, not for a consumer's reach — are the recorded " +
              std::to_string(kept.size()) + "; a new one is a decision that re-records this number");
    std::vector<std::string> unclassified, stale, roll_not_public, tool_internal, deleted_but_reached, internal_reached, misplaced, public_for_a_test;
    for (const std::string& f : declared)
      if (!cls.count(f)) unclassified.push_back(f);
    for (const Row& r : kApi) {
      if (!declared.count(r.fn)) { stale.push_back(r.fn); continue; }
      const std::string c = r.cls, reach = reach_of(r.fn);
      const bool pub_cls = (c == "PUBLIC");
      if (reach == "roll" && c != "PUBLIC") roll_not_public.push_back(std::string(r.fn) + " (" + c + ")");
      if (reach == "tools" && (c == "INTERNAL" || c == "DELETE")) tool_internal.push_back(std::string(r.fn) + " (" + c + ")");  /* `tools` is PAINT now */
      if (c == "DELETE" && reach != "nothing") deleted_but_reached.push_back(std::string(r.fn) + " (" + reach + ")");
      if (c == "INTERNAL" && (reach == "roll" || reach == "tools")) internal_reached.push_back(std::string(r.fn) + " (" + reach + ")");
      if (c == "INTERNAL" && public_only.count(r.fn)) internal_reached.push_back(std::string(r.fn) + " (a public-only suite)");
      // A ROW WHOSE ONLY JUSTIFICATION WOULD BE A TEST'S REACH FAILS.
      // If nothing but a test reaches a PUBLIC function, that test must be a PUBLIC-ONLY suite —
      // a program shaped like a consumer — because a test's reach is never itself a reason.
      if (pub_cls && reach == "tests" && !public_only.count(r.fn) && !kept.count(r.fn))
        public_for_a_test.push_back(std::string(r.fn) + " (" + c + ")");
      const bool pub = pub_cls;
      if (pub && !in_def.count(r.fn)) misplaced.push_back(std::string(r.fn) + " (" + c + ", not in rolltui.h)");
      if (!pub && in_def.count(r.fn)) misplaced.push_back(std::string(r.fn) + " (" + c + ", but in rolltui.h)");
      if (!pub && !in_internal.count(r.fn)) misplaced.push_back(std::string(r.fn) + " (" + c + ", not in any c/ header)");
    }
    auto join = [](const std::vector<std::string>& v) { std::string s; for (const std::string& x : v) s += "\n      " + x; return s; };
    check(unclassified.empty(), "every declared function has a class in api_classes.inc — a new one is a DECISION, not an arrival" + join(unclassified));
    check(stale.empty(), "every row of api_classes.inc names a declared function (a deleted one takes its row with it)" + join(stale));
    check(roll_not_public.empty(), "a function roll reaches is PUBLIC" + join(roll_not_public));
    check(tool_internal.empty(), "a function rolltui-paint reaches is PUBLIC — paint is a CONSUMER, the studio is not" + join(tool_internal));
    check(internal_reached.empty(), "an INTERNAL function is reached by no host, no tool and no public-only suite" + join(internal_reached));
    check(deleted_but_reached.empty(), "a DELETE row is reached by nothing, anywhere" + join(deleted_but_reached));
    check(public_for_a_test.empty(), "NO ROW IS PUBLIC FOR A TEST'S SAKE: a function only a test reaches is reached by a PUBLIC-ONLY suite" + join(public_for_a_test));
    check(misplaced.empty(), "THE DEFINITION IS WRITTEN FROM THE TABLE: every PUBLIC function is declared in rolltui.h and every INTERNAL one only under c/" + join(misplaced));
    std::map<std::string, int> totals;
    for (const Row& r : kApi) ++totals[r.cls];
    // MEASURED, and re-recorded DELIBERATELY whenever a row's class changes. What the two
    // classes MEAN is the part that has to survive a re-record:
    //
    //   PUBLIC   — reached by a host (roll, paint, the explorer), by the pure-C consumer or
    //              by a whole-host suite; OR kept on a STATED reason. The stated ones are
    //              the leak gauge and the named rungs, the functions a public C++ member in
    //              the definition calls (found by COMPILING it, which no reach bucket can
    //              do), and the four the sufficiency check in section 1 needs.
    //   INTERNAL — everything else. The STUDIO is not a consumer for this purpose: it is
    //              rolltui's own authoring tool and opts in like a suite. A unit test's or a
    //              meta test's reach is not a reason either.
    //
    // REACH IS THE INPUT AND NEVER THE CRITERION, because a consumer reaching THROUGH a bad
    // API looks identical to one reaching FOR a good one. The decision is a stated reason:
    // this is an entry point, or the vocabulary an entry point's signature speaks, or the
    // release half of a pair a consumer holds. Two cases reach cannot decide at all:
    //   - A NEW module has no callers by construction. Its rows are PUBLIC on a stated
    //     reason — a host author is the reader they exist for — or they are not public.
    //   - A function a host cannot REACH looks exactly like one nothing needs. The tell is
    //     rule 5: when a consumer hand-writes loops around a library function it could not
    //     get to, the API is wrong, not the consumer.
    //
    // A public input type whose value cannot be parsed is a contradiction in the surface,
    // which is why the colour parser and printer are public independently of the consumer
    // that found them missing.
    const int kPublic = 318, kInternal_ = 571, kDelete = 0;
    check(totals["PUBLIC"] == kPublic && totals["INTERNAL"] == kInternal_ && totals["DELETE"] == kDelete && totals["TOOL_FACING"] == 0,
          "the class totals are the recorded ones (PUBLIC " + std::to_string(totals["PUBLIC"]) +
              ", INTERNAL " + std::to_string(totals["INTERNAL"]) + ", DELETE " + std::to_string(totals["DELETE"]) +
              ", TOOL_FACING " + std::to_string(totals["TOOL_FACING"]) + ") — a moved class re-records them deliberately");
    if (std::getenv("ROLLTUI_CENSUS")) {
      std::map<std::string, std::map<std::string, int>> reach_by_class;
      for (const Row& r : kApi) ++reach_by_class[r.cls][reach_of(r.fn)];
      for (const auto& [c, m] : reach_by_class) {
        std::printf("        %-12s", c.c_str());
        for (const auto& [rch, n] : m) std::printf(" %s=%d", rch.c_str(), n);
        std::printf("\n");
      }
    }
  }

  // ---- 7. NO std:: CONTAINER OR VIEW UNDER __cplusplus, in the definition or under c/ ------
  // A C++ member may name rolltui's own types and the C standard's,
  // never a std:: container or view. The count is an exact zero, not a ceiling.
  {
    static const std::regex std_view(R"(\bstd::(string|string_view|vector|span|optional|map|set|function|unique_ptr|shared_ptr)\b)");
    std::vector<std::string> offenders;
    int cpp_lines = 0;
    std::vector<std::pair<std::string, std::string>> sources;
    sources.emplace_back("rolltui.h", text);
    for (const std::string& h : headers) sources.emplace_back("c/" + h, read(std::string(ROLLTUI_SOURCE_DIR) + "/c/" + h));
    for (const auto& [name, raw] : sources) {
      const std::string t = strip_all_comments(raw);
      std::istringstream in(t);
      std::string line;
      bool in_cpp = false;
      int depth = 0, lineno = 0;
      while (std::getline(in, line)) {
        ++lineno;
        const std::size_t at = line.find_first_not_of(" \t");
        const std::string s = at == std::string::npos ? std::string() : line.substr(at);
        if (s.rfind("#ifdef __cplusplus", 0) == 0 || s.rfind("#if defined(__cplusplus)", 0) == 0) { in_cpp = true; depth = 1; continue; }
        if (in_cpp && s.rfind("#if", 0) == 0) { ++depth; continue; }
        if (in_cpp && s.rfind("#endif", 0) == 0) { if (--depth == 0) in_cpp = false; continue; }
        if (!in_cpp) continue;
        ++cpp_lines;
        if (std::regex_search(s, std_view)) offenders.push_back(name + ":" + std::to_string(lineno) + ": " + s.substr(0, 80));
      }
    }
    check(cpp_lines > 500, "the __cplusplus scanner sees the C++ members (" + std::to_string(cpp_lines) + " lines)");
    check(std::regex_search(std::string("  std::string_view view() const;"), std_view) && !std::regex_search(std::string("  RolltuiStr s;"), std_view),
          "…and its pattern matches a std:: view and not rolltui's own type");
    std::string joined;
    for (const std::string& o : offenders) joined += "\n      " + o;
    check(offenders.empty(), "no __cplusplus member names a std:: container or view — rolltui's own types and the C standard's only" + joined);
  }

  // ---- 8. THE TOOL-FACING CLASS IS RETIRED, AND CANNOT COME BACK BY DRIFT ----
  // THE CLASS IS GONE, and the reason is the vocabulary error behind the question **"isn't
  // a tool a host?"** — it is. There are HOSTS (roll, the studio, paint, the explorer) and
  // there is one genuine other category, the EDITORS: models with no terminal in them that
  // the studio mounts inside itself. `TOOL_FACING` was measuring the DIRECTORY
  // `rolltui/tools/` rather than a concept, and of its 26 rows EIGHT were reached by
  // `studio.cpp` — a host, which the class said roll never reaches.
  //
  // With the studio reclassified as rolltui's own tool rather than a consumer, every one of
  // those 26 is reached only by the studio or an editor, so all of them are INTERNAL and the
  // class has nothing left to name. A returning banner would be a DECISION and must be made in
  // the open, not arrive with a section nobody re-read.
  {
    // BANNER lines only: a section banner's middle line sits between two `====` rules. The
    // paragraph at the top of the definition names the marker in prose to say it is retired,
    // and prose is not a banner — a scan that cannot tell them apart would fail on its own
    // explanation, which is a check that cannot be satisfied rather than one that holds.
    std::vector<std::string> marked;
    {
      std::vector<std::string> lines;
      for (std::size_t a = 0, b; a <= text.size(); a = b + 1) {
        b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        lines.push_back(text.substr(a, b - a));
        if (b == text.size()) break;
      }
      for (std::size_t i = 1; i < lines.size(); ++i)
        if (lines[i].find("[TOOL-FACING]") != std::string::npos && lines[i - 1].find("====") != std::string::npos)
          marked.push_back(lines[i]);
    }
    auto join = [](const std::vector<std::string>& v) { std::string s; for (const std::string& x : v) s += "\n      " + x; return s; };
    check(marked.empty(),
          "no [TOOL-FACING] banner remains in the definition — the class is retired, and a tool is a HOST" + join(marked));
    // The scanner is proved on a planted marker, so an empty result is a reading and not a miss.
    {  // the scanner proved on a planted banner, so an empty result is a reading and not a miss
      const std::string planted = "/* ====\n * theme_analysis [TOOL-FACING] — a returning class\n * ==== */\n";
      std::vector<std::string> lines;
      for (std::size_t a = 0, b; a <= planted.size(); a = b + 1) {
        b = planted.find('\n', a);
        if (b == std::string::npos) b = planted.size();
        lines.push_back(planted.substr(a, b - a));
        if (b == planted.size()) break;
      }
      int seen = 0;
      for (std::size_t i = 1; i < lines.size(); ++i)
        if (lines[i].find("[TOOL-FACING]") != std::string::npos && lines[i - 1].find("====") != std::string::npos) ++seen;
      check(seen == 1, "…and the banner scanner sees a planted one, so the empty result above is a reading");
    }
  }

  // ---- 9. WHICH READER IS IT FOR — and the header SECTIONED by the answer --
  // `api_classes.inc` says WHETHER a function is public. `api_roles.inc` says WHO IT IS FOR, and
  // this section holds `rolltui.h`'s physical layout to it. Without the placement check the roles
  // would be a comment nobody re-reads and the parts would drift back into module order.
  //
  // The three parts are found by their own banners, so RENAMING one fails here loudly rather than
  // silently emptying a bucket — the same reason section 6 looks for declarations at brace depth
  // zero instead of mentions.
  {
    struct RoleRow { const char* fn; const char* role; };
    static const RoleRow kRoles[] = {
#define ROLLTUI_ROLE(name, role) {#name, #role},
#include "api_roles.inc"
#undef ROLLTUI_ROLE
    };
    struct ClsRow { const char* fn; const char* cls; };
    static const ClsRow kCls[] = {
#define ROLLTUI_API(name, cls) {#name, #cls},
#include "api_classes.inc"
#undef ROLLTUI_API
    };
    const std::string hdr = read(std::string(ROLLTUI_SOURCE_DIR) + "/rolltui.h");
    const std::size_t p1 = hdr.find("PART 1 — THE NOUNS");
    const std::size_t p2 = hdr.find("PART 2 — THE HOST AUTHOR");
    const std::size_t p3 = hdr.find("PART 3 — THE WIDGET AUTHOR");
    check(p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos && p1 < p2 && p2 < p3,
          "rolltui.h is in three parts, in reader order: the nouns, the host author, the widget author");
    auto d0 = [](const std::string& t) {
      std::string out; std::vector<bool> counted; int depth = 0;
      for (std::size_t i = 0; i < t.size(); ++i) {
        const char ch = t[i];
        if (ch == '{') {
          const std::string before = t.substr(i >= 40 ? i - 40 : 0, i >= 40 ? 40 : i);
          const bool linkage = std::regex_search(before, std::regex(R"((extern\s+"C"|namespace\s+\w+)\s*$)"));
          counted.push_back(!linkage); if (!linkage) ++depth; continue;
        }
        if (ch == '}') { if (!counted.empty()) { if (counted.back()) --depth; counted.pop_back(); } continue; }
        if (depth == 0) out += ch;
      }
      return out;
    };
    std::set<std::string> part[3];
    if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
      const std::string chunk[3] = {hdr.substr(p1, p2 - p1), hdr.substr(p2, p3 - p2), hdr.substr(p3)};
      static const std::regex dre(R"(\b(rolltui_[a-z0-9_]+)\s*\()");
      for (int k = 0; k < 3; ++k) {
        const std::string t = d0(strip_all_comments(chunk[k]));
        for (std::sregex_iterator it(t.begin(), t.end(), dre), end; it != end; ++it) part[k].insert((*it)[1].str());
      }
    }
    std::map<std::string, std::string> role_of;
    for (const RoleRow& r : kRoles) role_of[r.fn] = r.role;
    std::vector<std::string> no_role, stale_role, misplaced_role, neither;
    std::map<std::string, std::string> cls_of;
    for (const ClsRow& r : kCls) cls_of[r.fn] = r.cls;
    for (const ClsRow& r : kCls)
      if (std::string(r.cls) == "PUBLIC" && !role_of.count(r.fn)) no_role.push_back(r.fn);
    for (const RoleRow& r : kRoles) {
      if (!cls_of.count(r.fn) || cls_of[r.fn] != "PUBLIC") { stale_role.push_back(r.fn); continue; }
      const std::string role = r.role;
      if (role == "NEITHER") { neither.push_back(r.fn); continue; }
      const int want = role == "VOCAB" ? 0 : role == "WIDGET" ? 2 : 1;
      if (!part[want].count(r.fn)) {
        int found = -1;
        for (int k = 0; k < 3; ++k) if (part[k].count(r.fn)) found = k;
        misplaced_role.push_back(std::string(r.fn) + " (" + role + ", declared in part " +
                                 (found < 0 ? std::string("none") : std::to_string(found + 1)) + ")");
      }
    }
    auto join = [](const std::vector<std::string>& v) { std::string s; for (const std::string& x : v) s += "\n      " + x; return s; };
    check(no_role.empty(), "every PUBLIC function has a ROLE — who is it for is a DECISION, not an arrival" + join(no_role));
    check(stale_role.empty(), "every role row names a PUBLIC function" + join(stale_role));
    check(neither.empty(), "NO PUBLIC FUNCTION FITS NO READER: NEITHER is a candidate for internal, not a resting place" + join(neither));
    check(misplaced_role.empty(), "THE HEADER IS SECTIONED BY THE ROLE: VOCAB in part 1, a host's in part 2, WIDGET in part 3" + join(misplaced_role));
    std::map<std::string, int> rt;
    for (const RoleRow& r : kRoles) ++rt[r.role];
    // MEASURED. Moving a role re-records these, which is the point: the SHAPE of the
    // surface becomes a number a reader can audit rather than an impression.
    //
    // WHAT MAKES A STAGE A STAGE, AND HOW A CATCH-ALL IS DETECTED. The stages follow one
    // sequence — load, settings, bind, run, release — plus the vocabulary they all speak
    // and the widget surface. A stage is honest when the families inside it are all doing
    // the SAME KIND of thing: HOST_BIND's are menu, bindings, input, context and windows,
    // every one a thing a host SUPPLIES; HOST_RUN's are windows, window, transcript,
    // terminal and swap, every one a thing a host DRIVES per frame.
    //
    // HOST_LOAD once failed that test, and the failure has a recognisable shape: it had
    // become the stage that anything touching a FILE landed in, a third of the whole API
    // under one word, so a reader asking "what do I need to start?" was handed 106
    // functions. Two subsystems filed there were not loading at all. The check is to name
    // each family in a large stage and ask whether it belongs to the same act; a stage that
    // sits OUTSIDE the sequence entirely is the strongest signal, because a category that
    // is not a stage of running a screen is visible as one thing rather than as N rows.
    //
    // A NAMED EMPTY STAGE STAYS IN THE TABLE. An empty row asserts that nothing is filed
    // there; deleting the row would make a future arrival unremarkable.
    check(rt["VOCAB"] == 33 && rt["HOST_LOAD"] == 28 && rt["HOST_SETTINGS"] == 57 &&
              rt["HOST_BIND"] == 86 && rt["HOST_RUN"] == 81 && rt["HOST_RELEASE"] == 7 &&
              rt["TOOL_INTEROP"] == 0 && rt["WIDGET"] == 26,
          "the roles are the recorded shape — vocab 33, host load 28 / settings 57 / bind 86 / run 81 / "
          "release 7, tool interop 0, widget 26 (got " +
              std::to_string(rt["VOCAB"]) + "/" + std::to_string(rt["HOST_LOAD"]) + "/" + std::to_string(rt["HOST_SETTINGS"]) +
              "/" + std::to_string(rt["HOST_BIND"]) + "/" + std::to_string(rt["HOST_RUN"]) + "/" +
              std::to_string(rt["HOST_RELEASE"]) + "/" + std::to_string(rt["TOOL_INTEROP"]) + "/" +
              std::to_string(rt["WIDGET"]) + ")");
    /* NO RATIO CHECK SITS HERE, AND THAT IS A MEASUREMENT RATHER THAN AN OMISSION. The obvious
     * guard against a stage becoming a catch-all again is "no stage is more than a fraction of
     * the surface", so it was written — and then aimed at the defect this milestone had just
     * fixed. A THIRD would have passed the old shape (106 of 326 is 32.5%), so it would have
     * caught nothing; a QUARTER would have caught it and now passes by two functions (81 of
     * 326, and 324 <= 326), so it would fail on the next ordinary addition for a reason that is
     * not this one. Neither is an instrument. The RECORDED SHAPE above is the ratchet: it fails
     * on any move at all, which is stronger than either threshold and never for the wrong
     * reason — the cost is that a human reads the diff, which is the right place for a
     * judgement about whether a stage still means one thing. */
    check(part[0].size() > 20 && part[1].size() > 200 && part[2].size() > 15,
          "…and the three parts are non-empty as read from the file (" + std::to_string(part[0].size()) + "/" +
              std::to_string(part[1].size()) + "/" + std::to_string(part[2].size()) + " declarations)");
  }
  // ---- 10. NO ORPHANED DOC COMMENT: a sentence in the header describes something it declares
  // ------------------------------------------------------------------------------------------
  // WHY A PUBLIC HEADER'S ORPHANED SENTENCE MATTERS MORE THAN A COMMENT USUALLY DOES: the
  // only reader who believes a header over the call sites is the one who cannot SEE the
  // call sites, which is exactly the consumer this header exists for. A declaration that
  // moves to an internal header takes the declaration and leaves the sentence, so the
  // definition documents functions it no longer declares — and the rule that covers it is
  // the general one, that whoever deletes a thing owns every sentence which described it.
  //
  // THE RULE HERE: a doc comment (not a `/* ---- section ---- */` banner) must be followed
  // by something OTHER than another doc comment. A comment with no declaration under it is
  // either a scar from a deletion or prose that belongs in a banner.
  {
    const std::string h = read(std::string(ROLLTUI_SOURCE_DIR) + "/rolltui.h");
    std::vector<std::string> lines;
    {
      std::istringstream in(h);
      std::string l;
      while (std::getline(in, l)) lines.push_back(l);
    }
    auto lstrip = [](const std::string& s2) {
      const std::size_t a = s2.find_first_not_of(" \t");
      return a == std::string::npos ? std::string() : s2.substr(a);
    };
    auto is_banner = [&](const std::string& s2) {
      const std::string t = lstrip(s2);
      return t.rfind("/* ----", 0) == 0 || t.rfind("/* ===", 0) == 0;
    };
    auto opens = [&](const std::string& s2) { return lstrip(s2).rfind("/*", 0) == 0 && !is_banner(s2); };
    int orphans = 0;
    std::string first;
    for (std::size_t i = 0; i < lines.size();) {
      if (!opens(lines[i])) { ++i; continue; }
      const std::size_t start = i;
      while (i < lines.size() && lines[i].find("*/") == std::string::npos) ++i;
      std::size_t j = i + 1;
      while (j < lines.size() && lstrip(lines[j]).empty()) ++j;
      if (j < lines.size() && opens(lines[j])) {
        ++orphans;
        if (first.empty()) first = lstrip(lines[start]).substr(0, 60);
      }
      ++i;
    }
    // A RATCHET at 6. It may FALL freely; a RISE means a declaration left the header and
    // its sentence stayed. A ratchet rather than a blanket `== 0` because the six that
    // remain are not scars: they are the legitimate pattern of two stacked comments above
    // one declaration, a general note and then a specific one.
    // `rolltui_preset_store_save_as` and `rolltui_widget_kind_count` are the clearest, and
    // the "EIGHT doors" note is prose introducing the block under it.
    check(orphans <= 6, "no orphaned doc comment in rolltui.h beyond the six stacked-note pairs — "
                        "a sentence with no declaration under it (" + std::to_string(orphans) +
                        " of at most 6; first: " + first + ")");
    // CONTROL: the scanner is armed — it still finds the six, and a planted one takes it to seven.
    check(orphans > 0, "…and the scanner is armed: it finds the six stacked-note pairs");
  }


  // rolltui draws for whoever asks and knows nothing about roll, so the dependency between
  // them runs ONE WAY. This is what says so. roll's headers are ENUMERATED from `include/`
  // rather than listed here, so a roll header added tomorrow is covered the day it lands.
  {
    std::vector<std::string> roll_headers;
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/../include", {".hpp"}, roll_headers);
    std::vector<std::string> names;
    for (const std::string& h : roll_headers) names.push_back(h.substr(h.rfind('/') + 1));
    check(names.size() > 5, "roll's own header set was found, so there is something to look for (" +
                            std::to_string(names.size()) + ")");
    if (!names.empty()) {
      auto roll_header_included_by = [&](const std::string& text) {
        for (const std::string& n : names)
          if (text.find("#include \"" + n + "\"") != std::string::npos ||
              text.find("#include <" + n + ">") != std::string::npos) return n;
        return std::string();
      };
      std::vector<std::string> lib;
      list_files(std::string(ROLLTUI_SOURCE_DIR), {".c", ".h", ".cpp", ".hpp"}, lib);
      std::string where, what;
      for (const std::string& f : lib) {
        if (f.find("/third_party/") != std::string::npos) continue;
        const std::string hit = roll_header_included_by(strip_all_comments(read(f)));
        if (!hit.empty()) { where = f; what = hit; break; }
      }
      check(what.empty(), "rolltui includes no roll header, so the dependency runs one way" +
                          (what.empty() ? std::string() : " (" + where + " includes " + what + ")"));
      // CONTROL: the matcher is armed. A planted include of a real roll header is found,
      // so the empty result above means "nothing there" rather than "nothing looked for".
      check(roll_header_included_by("#include \"" + names[0] + "\"\n") == names[0],
            "...and the matcher is armed: it finds a planted include of " + names[0]);
    }
  }

  return report("public_header_test");
}
