// rolltui/tests/public_header_test.cpp — the guarantees `rolltui/rolltui.h` makes about
// itself, as assertions rather than as a promise in its own comment.
//
// Since Phase 19 m2 the header is THE DEFINITION, and the guarantees are:
//   1. It COMPILES ALONE and gives you the API — this translation unit includes it and
//      nothing else of the library's, and calls it.
//   2. It DECLARES the public API and includes no `rolltui/c/` header: every PUBLIC and
//      TOOL_FACING function of api_classes.inc is declared in it and no INTERNAL one is
//      (section 6), the headers under c/ include it first and are included by no host or tool
//      (section 3, an exact zero over an explicit opt-in list of tests), and no C++ member
//      anywhere names a std:: container or view (section 7).
// It used to certify the opposite — a facade that "declares NOTHING of its own" — which is
// the mis-capture plan/phase-19.md records.
//
// The grep control proves itself against a planted line before it is trusted, because
// CLAUDE.md records `strings | grep MARKER` false-negativing twice in this repo and a
// scanner that cannot see a violation is worse than no scanner.
#include <cstdio>
#include <fstream>
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

// ---- section 6's instruments: a literal-aware comment stripper, a recursive lister, and the
// identifier scan the class table is checked against (Phase 19 m1) ---------------------------
// Comments are stripped RESPECTING string and char literals — a "/*" inside a JSON string or a
// "//" inside a URL would otherwise eat real code, which is how the first census of this
// surface under-counted the library's own reach by a third.
std::string strip_all_comments(const std::string& src) {
  std::string out;
  out.reserve(src.size());
  for (size_t i = 0; i < src.size();) {
    const char c = src[i];
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < src.size() && src[j] != c) j += (src[j] == '\\') ? 2 : 1;
      out.append(src, i, std::min(j + 1, src.size()) - i);
      i = j + 1;
    } else if (src.compare(i, 2, "/*") == 0) {
      const size_t j = src.find("*/", i + 2);
      out += ' ';
      i = (j == std::string::npos) ? src.size() : j + 2;
    } else if (src.compare(i, 2, "//") == 0) {
      const size_t j = src.find('\n', i);
      i = (j == std::string::npos) ? src.size() : j;
    } else {
      out += c;
      ++i;
    }
  }
  return out;
}

// PHASE 20 m1: for REACH, a string literal's CONTENTS are blanked as well. An identifier inside
// a literal is DATA, never a call — `ownership_test.cpp` names `rolltui_mem_realloc` inside a
// regex and `budget_test.cpp` names it in prose, and the first run of the public-only rule below
// reported both as consumers of an internal function. Comments already went; literals had to go
// too, and this is general rather than a special case for the meta-tests: nothing anywhere calls
// a function by naming it in a string.
std::string strip_comments_and_literals(const std::string& src) {
  const std::string t = strip_all_comments(src);
  std::string out;
  out.reserve(t.size());
  for (size_t i = 0; i < t.size();) {
    const char c = t[i];
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < t.size() && t[j] != c) j += (t[j] == '\\') ? 2 : 1;
      out += ' ';
      i = j + 1;
    } else {
      out += c;
      ++i;
    }
  }
  return out;
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
  RolltuiFrame* f = rolltui_frame_new(4, 2, RolltuiStyle{});
  check(f != nullptr && rolltui_frame_width(f) == 4, "a frame, from the umbrella header alone");
  RolltuiStr out{};
  rolltui_frame_to_text(f, &out);
  size_t n = 0;
  const char* txt = rolltui_str_get(&out, &n);
  check(txt != nullptr && n > 0, "…and rendering it to text, which needs three headers working together");
  rolltui_str_free(&out);
  rolltui_frame_free(f);

  RolltuiSwap* s = rolltui_swap_new(4, 2, RolltuiStyle{});
  check(s != nullptr, "…and the double buffer, which is the newest entry point");
  rolltui_swap_free(s);

  // ---- 2. it DECLARES the public API (Phase 19 m2) ----------------------------------------
  // Until Phase 19 this check read "declares NOTHING of its own", and it enforced a facade the
  // user never chose (the record in plan/phase-19.md). The definition is certified the other
  // way round: this file carries the declarations, every PUBLIC and TOOL_FACING function of
  // api_classes.inc is declared HERE and no INTERNAL one is (section 6), and it includes no
  // `rolltui/c/` header at all.
  const std::vector<std::string> decls = declarations(text);
  check(decls.size() > 1500, "rolltui.h DECLARES the API: " + std::to_string(decls.size()) + " declaration lines of its own");
  check(text.find("#include \"rolltui/c/") == std::string::npos, "…and includes no rolltui/c/ header — the definition is the file, not a list");
  check(declarations("#include \"x.h\"\n// a comment\nint rolltui_planted(void);\n").size() == 1,
        "…and the scanner catches a planted declaration, so the count above means something");

  // ---- 3. the headers under c/ are the library's own ---------------------------------------
  // Enumerated from the directory. Each includes the definition FIRST (one definition per
  // type holds by construction), and none is included by a host or a tool. A TEST may include
  // one — by naming it, with the comment the opt-in carries, and by being listed in
  // rolltui/CMakeLists.txt's ROLLTUI_INTERNAL_TESTS. The count outside that list is 0, asserted.
  std::vector<std::string> headers;
  if (DIR* d = opendir((std::string(ROLLTUI_SOURCE_DIR) + "/c").c_str())) {
    while (dirent* e = readdir(d)) {
      const std::string n = e->d_name;
      if (n.size() > 2 && n.compare(n.size() - 2, 2, ".h") == 0) headers.push_back(n);
    }
    closedir(d);
  }
  std::sort(headers.begin(), headers.end());
  // RECORDED (Phase 19 m3): 39 headers under c/ before m3, 24 after — the 48 DELETE functions
  // went, and every header that was left with nothing but its guard went with them (rule: a
  // header exists because a .c needs a declaration from it; one that declares nothing is a
  // file with no reason, and the check below keeps it that way).
  // 24 -> 27 (Phase 20 m3): `rolltui_lifetime.h`, `rolltui_render.h` and `rolltui_wrap.h` were
  // RE-CREATED. m3's rule ran in reverse — a header exists because a `.c` needs a declaration
  // from it, and moving those modules' steps out of the definition gave each a declaration again.
  const std::size_t kInternalHeaders = 27;
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
      std::string list = ROLLTUI_INTERNAL_TESTS;
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
    list_files(std::string(ROLLTUI_SOURCE_DIR) + "/tests", {".cpp", ".hpp", ".c"}, files);
    std::vector<std::string> offenders;
    int opted = 0;
    for (const std::string& f : files) {
      if (strip_all_comments(read(f)).find("#include \"rolltui/c/") == std::string::npos) continue;
      const std::string base = f.substr(f.find_last_of('/') + 1);
      const bool is_test = f.find(std::string(ROLLTUI_SOURCE_DIR) + "/tests/") != std::string::npos;
      if (is_test && optin.count(base)) { ++opted; continue; }
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
  // It counted consumers including a C++ `rolltui/*.hpp`; none exist since Phase 17 m2c, and
  // section 3 above counts the thing that matters now — direct `rolltui/c/` includes — as an
  // exact zero rather than a ceiling.

  // ---- m4b: NO PUBLIC FUNCTION HANDS A RESULT BACK THROUGH A CALLBACK ---------------------
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
  //         it (Phase 19 m1, flipped in m2) ------------------------------------------------
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
    auto depth0 = [](const std::string& t) {
      std::string out;
      std::vector<bool> counted;
      int depth = 0;
      for (std::size_t i = 0; i < t.size(); ++i) {
        const char ch = t[i];
        if (ch == '{') {
          const std::string before = t.substr(i >= 40 ? i - 40 : 0, i >= 40 ? 40 : i);
          const bool linkage = std::regex_search(before, std::regex(R"((extern\s+"C"|namespace\s+\w+)\s*$)"));
          counted.push_back(!linkage);
          if (!linkage) ++depth;
          continue;
        }
        if (ch == '}') {
          if (!counted.empty()) { if (counted.back()) --depth; counted.pop_back(); }
          continue;
        }
        if (depth == 0) out += ch;
      }
      return out;
    };
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
    const std::set<std::string> roll = mentions_in({repo + "/src", repo + "/include"}, {".cpp", ".hpp"});
    const std::set<std::string> tools = mentions_in({root + "/tools"}, {".cpp", ".hpp"});
    const std::set<std::string> tests = mentions_in({root + "/tests", repo + "/tests"}, {".cpp", ".hpp", ".c"});
    // PHASE 20 m1: the PUBLIC-ONLY suites — programs shaped like a CONSUMER, which include
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
    check(declared.size() > 700 && roll.count("rolltui_preset_store_new") && tools.count("rolltui_window_stack_push_popup") &&
              lib.count("rolltui_str_append") && !lib.count("rolltui_preset_store_new_NOSUCH") && in_def.size() > 400 && in_internal.size() > 300,
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
    check(kept.size() == 15,
          "the KEPT rows — PUBLIC for a stated reason, not for a consumer's reach — are the recorded " +
              std::to_string(kept.size()) + "; a new one is a decision that re-records this number");
    std::vector<std::string> unclassified, stale, roll_not_public, tool_internal, deleted_but_reached, internal_reached, misplaced, public_for_a_test;
    for (const std::string& f : declared)
      if (!cls.count(f)) unclassified.push_back(f);
    for (const Row& r : kApi) {
      if (!declared.count(r.fn)) { stale.push_back(r.fn); continue; }
      const std::string c = r.cls, reach = reach_of(r.fn);
      const bool pub_cls = (c == "PUBLIC" || c == "TOOL_FACING");
      if (reach == "roll" && c != "PUBLIC") roll_not_public.push_back(std::string(r.fn) + " (" + c + ")");
      if (reach == "tools" && (c == "INTERNAL" || c == "DELETE")) tool_internal.push_back(std::string(r.fn) + " (" + c + ")");
      if (c == "DELETE" && reach != "nothing") deleted_but_reached.push_back(std::string(r.fn) + " (" + reach + ")");
      if (c == "INTERNAL" && (reach == "roll" || reach == "tools")) internal_reached.push_back(std::string(r.fn) + " (" + reach + ")");
      if (c == "INTERNAL" && public_only.count(r.fn)) internal_reached.push_back(std::string(r.fn) + " (a public-only suite)");
      // PHASE 20 m1, THE DONE-WHEN: a row whose ONLY justification would be a test's reach fails.
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
    check(tool_internal.empty(), "a function a tool reaches is PUBLIC or TOOL_FACING" + join(tool_internal));
    check(internal_reached.empty(), "an INTERNAL function is reached by no host, no tool and no public-only suite" + join(internal_reached));
    check(deleted_but_reached.empty(), "a DELETE row is reached by nothing, anywhere" + join(deleted_but_reached));
    check(public_for_a_test.empty(), "NO ROW IS PUBLIC FOR A TEST'S SAKE: a function only a test reaches is reached by a PUBLIC-ONLY suite" + join(public_for_a_test));
    check(misplaced.empty(), "THE DEFINITION IS WRITTEN FROM THE TABLE: every PUBLIC and TOOL_FACING function is declared in rolltui.h and every INTERNAL one only under c/" + join(misplaced));
    std::map<std::string, int> totals;
    for (const Row& r : kApi) ++totals[r.cls];
    // MEASURED 2026-09-06 (Phase 19 m1), re-recorded in m2 for the four functions a public
    // C++ member calls, the one the C consumer reaches, and the 19 allocator/map rows the
    // widened census (every header under c/) added as INTERNAL; DELETE 48 -> 0 in m3, the functions gone.
    // PHASE 20 m1/m2: 582/42/199 -> 454/26/343. 146 functions moved PUBLIC or TOOL_FACING ->
    // INTERNAL: a test's reach is no longer a reason, and a type's lifecycle is public only when a
    // CONSUMER holds the type. What is left public is reached by a host, a tool or a public-only
    // suite, or carries a KEPT reason (15 of those, counted above).
    const int kPublic = 454, kTool = 26, kInternal_ = 343, kDelete = 0;
    check(totals["PUBLIC"] == kPublic && totals["TOOL_FACING"] == kTool && totals["INTERNAL"] == kInternal_ && totals["DELETE"] == kDelete,
          "the class totals are the recorded ones (PUBLIC " + std::to_string(totals["PUBLIC"]) + ", TOOL_FACING " + std::to_string(totals["TOOL_FACING"]) +
              ", INTERNAL " + std::to_string(totals["INTERNAL"]) + ", DELETE " + std::to_string(totals["DELETE"]) + ") — a moved class re-records them deliberately");
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
  // The user's rule, 2026-09-06: a C++ member may name rolltui's own types and the C standard's,
  // never a std:: container or view. 117 such lines were cut in m2; this keeps the count at zero.
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

  // ---- 8. THE TOOL-FACING SET HAS A HOME: sections of THIS header, marked (Phase 19 m4) ----
  // m1 decided one header with marked sections over a second `rolltui_tools.h`; this holds the
  // marking to the class table in both directions. A section is the text between two
  // `/* ====` banners, and it is tool-facing when its banner line carries `[TOOL-FACING]`.
  {
    struct Row { const char* fn; const char* cls; };
    static const Row kApi[] = {
#define ROLLTUI_API(name, cls) {#name, #cls},
#include "api_classes.inc"
#undef ROLLTUI_API
    };
    std::map<std::string, std::string> cls;
    for (const Row& r : kApi) cls[r.fn] = r.cls;
    static const std::regex decl_re(R"(\b(rolltui_[a-z0-9_]+)\s*\()");
    std::vector<std::pair<std::string, bool>> sections;  // (raw text, tool-facing)
    {
      std::size_t at = 0;
      std::size_t pos = text.find("/* =====");
      while (pos != std::string::npos) {
        const std::size_t next = text.find("/* =====", pos + 8);
        const std::string chunk = text.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        const std::size_t eol = chunk.find('\n');
        const std::string banner = chunk.substr(0, chunk.find('\n', eol + 1));
        sections.emplace_back(chunk, banner.find("[TOOL-FACING]") != std::string::npos);
        pos = next;
        (void)at;
      }
    }
    int marked = 0, located_tool = 0;
    std::vector<std::string> tool_outside, public_inside;
    for (const auto& [raw, tool] : sections) {
      marked += tool ? 1 : 0;
      const std::string t = strip_all_comments(raw);
      // depth zero, as in section 6: a member body's mention is not a declaration
      std::string d0;
      {
        std::vector<bool> counted;
        int depth = 0;
        for (std::size_t i = 0; i < t.size(); ++i) {
          const char ch = t[i];
          if (ch == '{') {
            const std::string before = t.substr(i >= 40 ? i - 40 : 0, i >= 40 ? 40 : i);
            const bool linkage = std::regex_search(before, std::regex(R"((extern\s+"C"|namespace\s+\w+)\s*$)"));
            counted.push_back(!linkage);
            if (!linkage) ++depth;
            continue;
          }
          if (ch == '}') { if (!counted.empty()) { if (counted.back()) --depth; counted.pop_back(); } continue; }
          if (depth == 0) d0 += ch;
        }
      }
      for (std::sregex_iterator it(d0.begin(), d0.end(), decl_re), end; it != end; ++it) {
        const std::string f = (*it)[1].str();
        const auto c = cls.find(f);
        if (c == cls.end()) continue;
        if (c->second == "TOOL_FACING") { if (tool) ++located_tool; else tool_outside.push_back(f); }
        if (c->second == "PUBLIC" && tool) public_inside.push_back(f);
      }
    }
    auto join = [](const std::vector<std::string>& v) { std::string s; for (const std::string& x : v) s += "\n      " + x; return s; };
    // 40 -> 26 (Phase 20 m1): sixteen theme-analysis steps went INTERNAL, so the three
    // [TOOL-FACING] sections declare 26 — every one of them reached by a tool.
    check(sections.size() > 20 && marked == 3 && located_tool >= 26,
          "the section scanner sees the banners (" + std::to_string(sections.size()) + "), the three marked [TOOL-FACING], and " +
              std::to_string(located_tool) + " tool-facing declarations under them");
    check(tool_outside.empty(), "every TOOL_FACING function is declared under a [TOOL-FACING] banner" + join(tool_outside));
    check(public_inside.empty(), "no PUBLIC function is declared under a [TOOL-FACING] banner — a host never needs one from there" + join(public_inside));
  }

  return report("public_header_test");
}
