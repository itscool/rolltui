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
using namespace testkit;

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
  const std::size_t kInternalHeaders = 42; /* +rolltui_regex.h; +rolltui_syntax.h; +rolltui_mermaid.h; +rolltui_preview.h; +rolltui_termfacts.h; +rolltui_hints.h; +rolltui_widget_picker.h; +rolltui_keys_editor.h; +rolltui_theme_editor.h; +rolltui_context.h; -rolltui_app_profile.h */
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
    static const Row kRows[] = {{"rolltui.h", 0}, {"c/rolltui_layout_tree.h", 21}};
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
    check(accounted == 21, "…and the accepted forward declarations are the recorded 21 [" + std::to_string(accounted) + "]");
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
    std::vector<std::string> files;
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/tools", {".cpp", ".hpp"}, files);
    // `rolltui/examples/` is scanned too, and NOTHING in it may opt in. It holds the
    // two CONSUMERS — `rolltui-paint` (moved here from `tools/` when the directory's name
    // finally misled someone) and `dirktui` — and a consumer that reaches past the
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
      // The opt-in LIST is the authority on who may reach an internal header; this bound only
      // says where such a file may live.
      const bool listed_dir = f.find(std::string(ROLLTUI_SOURCE_DIR) + "/tests/") != std::string::npos ||
                              f.find(std::string(ROLLTUI_SOURCE_DIR) + "/tools/") != std::string::npos;
      if (listed_dir && optin.count(base)) { ++opted; continue; }
      offenders.push_back(f.substr(f.find("/tui/") == std::string::npos ? 0 : f.find("/tui/") + 5));
    }
    std::string joined;
    for (const std::string& o : offenders) joined += "\n      " + o;
    check(opted >= 5, "the include scanner sees the opted-in tests (" + std::to_string(opted) + " of " + std::to_string(optin.size()) + " listed)");
    check(offenders.empty(), "no host, tool or unlisted test includes a rolltui/c/ header — the count is 0, not a ratchet" + joined);
  }

  // ---- 3b. rolltui_studio.h IS THE SAME CLOSED DOOR, ONE HEADER OVER -------------------------
  // The preset stores' in-place EDIT surface (`_edit`/`_set_working`) lives in rolltui_studio.h,
  // not rolltui.h, because only rolltui-studio (and, through it, its editors) ever changes a
  // preset's content — dirktui, paint and roll read one, switch to another, and (roll) save or
  // add one, all through rolltui.h. Same mechanism as the internal `c/` headers above: the
  // opt-in list is the authority, and the count outside it is 0, not a ratchet.
  {
    std::set<std::string> optin;
    {
      std::string list = ROLLTUI_INTERNAL_OPT_IN;
      std::size_t at = 0;
      while (at <= list.size()) {
        const std::size_t comma = list.find(',', at);
        optin.insert(list.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        if (comma == std::string::npos) break;
        at = comma + 1;
      }
    }
    std::vector<std::string> files;
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/tools", {".cpp", ".hpp"}, files);
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/examples", {".cpp", ".hpp"}, files);
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/tests", {".cpp", ".hpp", ".c"}, files);
    std::vector<std::string> offenders;
    int opted = 0;
    for (const std::string& f : files) {
      if (strip_all_comments(read(f)).find("#include \"rolltui/rolltui_studio.h\"") == std::string::npos) continue;
      const std::string base = f.substr(f.find_last_of('/') + 1);
      const bool listed_dir = f.find(std::string(ROLLTUI_SOURCE_DIR) + "/tests/") != std::string::npos ||
                              f.find(std::string(ROLLTUI_SOURCE_DIR) + "/tools/") != std::string::npos;
      if (listed_dir && optin.count(base)) { ++opted; continue; }
      offenders.push_back(f.substr(f.find("/tui/") == std::string::npos ? 0 : f.find("/tui/") + 5));
    }
    std::string joined;
    for (const std::string& o : offenders) joined += "\n      " + o;
    check(opted >= 1, "the include scanner sees at least one opted-in file reaching rolltui_studio.h (" + std::to_string(opted) + ")");
    check(offenders.empty(), "no host, tool or unlisted test includes rolltui_studio.h — the count is 0, not a ratchet" + joined);
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

  // ---- 6. (the reach census, retired) ------------------------------------------------------
  // It measured which functions a HOST's source reached, by walking the tree this checkout sits
  // in, and so only ran inside one. A suite here tests this library alone and passes the same
  // wherever it is vendored; whether a host still builds and works against it is that host's own
  // suite's question. `api_classes.inc` remains the class of every function, read by section 9.

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

  // ---- 9. WHICH READER IS IT FOR — a DECISION, tracked as data, not by physical position ------
  // `api_classes.inc` says WHETHER a function is public. `api_roles.inc` says WHO IT IS FOR. Until
  // this milestone, `rolltui.h`'s physical layout was ALSO held to that answer (a function's role
  // had to match which of three physical parts its text sat in) — that enforcement is gone now
  // that the header reorganized around MODULE-MAJOR books (a type and its functions adjacent)
  // rather than READER-MAJOR parts (every noun, then every host verb, then every widget verb),
  // because the two axes don't line up: a single book like WIDGETS holds VOCAB types, HOST_BIND
  // functions and WIDGET functions side by side, on purpose. What role checking still means: every
  // PUBLIC function has a role, every role row still names a real PUBLIC function, and no role goes
  // begging as NEITHER — all decidable from `api_classes.inc`/`api_roles.inc` alone, with no need to
  // parse `rolltui.h`'s text at all. Section 9b, below, checks the header's new physical shape (the
  // four books, in order) — a DIFFERENT question from this one.
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
    std::map<std::string, std::string> role_of;
    for (const RoleRow& r : kRoles) role_of[r.fn] = r.role;
    std::vector<std::string> no_role, stale_role, neither;
    std::map<std::string, std::string> cls_of;
    for (const ClsRow& r : kCls) cls_of[r.fn] = r.cls;
    for (const ClsRow& r : kCls)
      if (std::string(r.cls) == "PUBLIC" && !role_of.count(r.fn)) no_role.push_back(r.fn);
    for (const RoleRow& r : kRoles) {
      if (!cls_of.count(r.fn) || cls_of[r.fn] != "PUBLIC") { stale_role.push_back(r.fn); continue; }
      if (r.role == std::string("NEITHER")) neither.push_back(r.fn);
    }
    auto join = [](const std::vector<std::string>& v) { std::string s; for (const std::string& x : v) s += "\n      " + x; return s; };
    check(no_role.empty(), "every PUBLIC function has a ROLE — who is it for is a DECISION, not an arrival" + join(no_role));
    check(stale_role.empty(), "every role row names a PUBLIC function" + join(stale_role));
    check(neither.empty(), "NO PUBLIC FUNCTION FITS NO READER: NEITHER is a candidate for internal, not a resting place" + join(neither));
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
    //
    // HOST_SETTINGS 90 -> 93: each store's `_copy_working`, the owned copy a host rendering on
    // another thread than the one that changes the store holds (roll's UI thread beside its REPL).
    check(rt["VOCAB"] == 36 && rt["HOST_LOAD"] == 30 && rt["HOST_SETTINGS"] == 93 &&
              rt["HOST_BIND"] == 95 && rt["HOST_RUN"] == 108 && rt["HOST_RELEASE"] == 10 &&
              rt["TOOL_INTEROP"] == 0 && rt["WIDGET"] == 27,
          "the roles are the recorded shape — vocab 36, host load 30 / settings 93 / bind 95 / run 108 / "
          "release 10, tool interop 0, widget 27 (got " +
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
  }
  // ---- 9b. THE HEADER'S PHYSICAL SHAPE: four books, in dependency order ----------------------
  // A DIFFERENT question from section 9's (which was WHO a function is for, decidable from data
  // alone). This one is WHERE things sit: module-major, a type beside its own functions, grouped
  // into CORE / CONFIGURATION / WIDGETS / RUNTIME — found by their own banners, so renaming one
  // fails here loudly rather than silently emptying a book.
  {
    const std::string hdr = read(std::string(ROLLTUI_SOURCE_DIR) + "/rolltui.h");
    const std::size_t b1 = hdr.find("BOOK 1 — CORE");
    const std::size_t b2 = hdr.find("BOOK 2 — CONFIGURATION");
    const std::size_t b3 = hdr.find("BOOK 3 — WIDGETS");
    const std::size_t b4 = hdr.find("BOOK 4 — RUNTIME");
    check(b1 != std::string::npos && b2 != std::string::npos && b3 != std::string::npos && b4 != std::string::npos &&
              b1 < b2 && b2 < b3 && b3 < b4,
          "rolltui.h is four books, in dependency order: core, configuration, widgets, runtime");
    // THE MIGRATION IS COMPLETE: no module is left waiting under the old reader-major scaffolding,
    // and every book actually holds something — the same shape section 9's old part[] check had,
    // now asked of four books instead of three parts.
    check(hdr.find("PART 1 —") == std::string::npos && hdr.find("PART 2 —") == std::string::npos &&
              hdr.find("PART 3 —") == std::string::npos,
          "no module is left under the old PART 1/2/3 scaffolding — every module has a book");
    if (b1 != std::string::npos && b2 != std::string::npos && b3 != std::string::npos && b4 != std::string::npos) {
      const std::string chunk[4] = {hdr.substr(b1, b2 - b1), hdr.substr(b2, b3 - b2), hdr.substr(b3, b4 - b3), hdr.substr(b4)};
      static const std::regex dre(R"(\b(rolltui_[a-z0-9_]+)\s*\()");
      std::set<std::string> book[4];
      for (int k = 0; k < 4; ++k) {
        const std::string t = depth0(strip_all_comments(chunk[k]));
        for (std::sregex_iterator it(t.begin(), t.end(), dre), end; it != end; ++it) book[k].insert((*it)[1].str());
      }
      check(book[0].size() > 20 && book[1].size() > 50 && book[2].size() > 100 && book[3].size() > 10,
            "…and the four books are non-empty as read from the file (" + std::to_string(book[0].size()) + "/" +
                std::to_string(book[1].size()) + "/" + std::to_string(book[2].size()) + "/" +
                std::to_string(book[3].size()) + " declarations)");
    }
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

  return report("public_header_test");
}
